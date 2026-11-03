# SPDX-License-Identifier: MIT OR Apache-2.0
"""Bounded, shell-free child execution. No subprocess work occurs on import."""
from __future__ import annotations

import ctypes
from ctypes import wintypes
from dataclasses import dataclass
import os
from pathlib import Path
import signal
import shutil
import subprocess
import threading
import time
from typing import Mapping, Sequence

WINDOWS_CREATION_LOCK = threading.Lock()
_CANCELLED = threading.Event()
_HANDLERS_INSTALLED = False


def install_cancellation_handlers() -> None:
    """Defer SIGINT/SIGTERM until every active run has killed its tree."""
    global _HANDLERS_INSTALLED
    if _HANDLERS_INSTALLED or threading.current_thread() is not threading.main_thread():
        return

    def cancel(signum, frame):
        _CANCELLED.set()

    for signum in (signal.SIGINT, signal.SIGTERM):
        signal.signal(signum, cancel)
    _HANDLERS_INSTALLED = True


@dataclass
class Result:
    argv: list[str]
    cwd: str
    returncode: int | None
    duration: float
    stdout: bytes
    stderr: bytes
    failure: str | None = None


class WindowsChild:
    """Create suspended, assign to kill-on-close Job, then resume the thread."""
    def __init__(self, argv: Sequence[str], cwd: Path, env: Mapping[str, str]):
        import msvcrt
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        api = self.api

        class Startup(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("reserved", wintypes.LPWSTR),
                        ("desktop", wintypes.LPWSTR), ("title", wintypes.LPWSTR),
                        ("x", wintypes.DWORD), ("y", wintypes.DWORD),
                        ("xs", wintypes.DWORD), ("ys", wintypes.DWORD),
                        ("xc", wintypes.DWORD), ("yc", wintypes.DWORD),
                        ("fill", wintypes.DWORD), ("flags", wintypes.DWORD),
                        ("show", wintypes.WORD), ("reserved_size", wintypes.WORD),
                        ("reserved_data", ctypes.c_void_p), ("stdin", wintypes.HANDLE),
                        ("stdout", wintypes.HANDLE), ("stderr", wintypes.HANDLE)]

        class ProcessInfo(ctypes.Structure):
            _fields_ = [("process", wintypes.HANDLE), ("thread", wintypes.HANDLE),
                        ("pid", wintypes.DWORD), ("tid", wintypes.DWORD)]

        class BasicLimits(ctypes.Structure):
            _fields_ = [("process_time", ctypes.c_int64), ("job_time", ctypes.c_int64),
                        ("flags", wintypes.DWORD), ("minimum", ctypes.c_size_t),
                        ("maximum", ctypes.c_size_t), ("active", wintypes.DWORD),
                        ("affinity", ctypes.c_size_t), ("priority", wintypes.DWORD),
                        ("scheduling", wintypes.DWORD)]

        class Counters(ctypes.Structure):
            _fields_ = [(name, ctypes.c_uint64) for name in
                        ("read_ops", "write_ops", "other_ops", "read_bytes", "write_bytes", "other_bytes")]

        class Limits(ctypes.Structure):
            _fields_ = [("basic", BasicLimits), ("io", Counters),
                        ("process_limit", ctypes.c_size_t), ("job_limit", ctypes.c_size_t),
                        ("peak_process", ctypes.c_size_t), ("peak_job", ctypes.c_size_t)]

        signatures = {
            "CreateJobObjectW": ([ctypes.c_void_p, wintypes.LPCWSTR], wintypes.HANDLE),
            "SetInformationJobObject": ([wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD], wintypes.BOOL),
            "AssignProcessToJobObject": ([wintypes.HANDLE, wintypes.HANDLE], wintypes.BOOL),
            "CreateProcessW": ([wintypes.LPCWSTR, wintypes.LPWSTR, ctypes.c_void_p, ctypes.c_void_p,
                                wintypes.BOOL, wintypes.DWORD, ctypes.c_void_p, wintypes.LPCWSTR,
                                ctypes.POINTER(Startup), ctypes.POINTER(ProcessInfo)], wintypes.BOOL),
            "ResumeThread": ([wintypes.HANDLE], wintypes.DWORD),
            "GetExitCodeProcess": ([wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)], wintypes.BOOL),
            "WaitForSingleObject": ([wintypes.HANDLE, wintypes.DWORD], wintypes.DWORD),
            "TerminateJobObject": ([wintypes.HANDLE, wintypes.UINT], wintypes.BOOL),
            "TerminateProcess": ([wintypes.HANDLE, wintypes.UINT], wintypes.BOOL),
            "CloseHandle": ([wintypes.HANDLE], wintypes.BOOL),
        }
        for name, (args, result) in signatures.items():
            getattr(api, name).argtypes = args
            getattr(api, name).restype = result
        self.job = api.CreateJobObjectW(None, None)
        self.process = None
        self.stdout = self.stderr = None
        if not self.job:
            raise ctypes.WinError(ctypes.get_last_error())
        handles: list[int] = []
        fds: list[int] = []
        info = ProcessInfo()
        try:
            limits = Limits()
            limits.basic.flags = 0x2000  # JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            if not api.SetInformationJobObject(self.job, 9, ctypes.byref(limits), ctypes.sizeof(limits)):
                raise ctypes.WinError(ctypes.get_last_error())
            out_read, out_write = os.pipe()
            fds.extend([out_read, out_write])
            err_read, err_write = os.pipe()
            fds.extend([err_read, err_write])
            with open(os.devnull, "rb") as null:
                for fd in (out_write, err_write, null.fileno()):
                    os.set_handle_inheritable(msvcrt.get_osfhandle(fd), True)
                startup = Startup()
                startup.cb = ctypes.sizeof(startup)
                startup.flags = 0x100  # STARTF_USESTDHANDLES
                startup.stdin = msvcrt.get_osfhandle(null.fileno())
                startup.stdout = msvcrt.get_osfhandle(out_write)
                startup.stderr = msvcrt.get_osfhandle(err_write)
                command = ctypes.create_unicode_buffer(subprocess.list2cmdline(list(argv)))
                environment = ctypes.create_unicode_buffer(
                    "\0".join(f"{key}={value}" for key, value in sorted(env.items(), key=lambda x: x[0].upper())) + "\0\0")
                flags = 0x4 | 0x400 | 0x8000000  # SUSPENDED | UNICODE_ENVIRONMENT | NO_WINDOW
                if not api.CreateProcessW(str(argv[0]), command, None, None, True, flags,
                                          environment, str(cwd), ctypes.byref(startup), ctypes.byref(info)):
                    raise ctypes.WinError(ctypes.get_last_error())
            self.process = info.process
            handles.append(info.thread)
            if not api.AssignProcessToJobObject(self.job, self.process):
                raise ctypes.WinError(ctypes.get_last_error())
            if api.ResumeThread(info.thread) == 0xFFFFFFFF:
                raise ctypes.WinError(ctypes.get_last_error())
            os.close(out_write)
            fds.remove(out_write)
            os.close(err_write)
            fds.remove(err_write)
            self.stdout = os.fdopen(out_read, "rb", buffering=0)
            fds.remove(out_read)
            self.stderr = os.fdopen(err_read, "rb", buffering=0)
            fds.remove(err_read)
        except BaseException:
            if self.process:
                api.TerminateProcess(self.process, 1)
            self.close()
            for pipe in (self.stdout, self.stderr):
                if pipe is not None:
                    pipe.close()
            raise
        finally:
            for handle in handles:
                api.CloseHandle(handle)
            for fd in fds:
                os.close(fd)

    def poll(self) -> int | None:
        wait = self.api.WaitForSingleObject(self.process, 0)
        if wait == 0x102:
            return None
        if wait != 0:
            raise ctypes.WinError(ctypes.get_last_error())
        status = wintypes.DWORD()
        if not self.api.GetExitCodeProcess(self.process, ctypes.byref(status)):
            raise ctypes.WinError(ctypes.get_last_error())
        return status.value

    def kill(self) -> None:
        if self.job and not self.api.TerminateJobObject(self.job, 1):
            raise ctypes.WinError(ctypes.get_last_error())

    def close(self) -> None:
        if self.job:
            self.api.CloseHandle(self.job)
            self.job = None
        if self.process:
            self.api.CloseHandle(self.process)
            self.process = None


def run(argv: Sequence[str], cwd: Path, timeout: float = 30,
        output_limit: int = 2 * 1024 * 1024,
        env: Mapping[str, str] | None = None) -> Result:
    started = time.monotonic()
    command = [str(arg) for arg in argv]
    if not command or timeout <= 0 or output_limit <= 0:
        raise ValueError("a command and positive process budgets are required")
    install_cancellation_handlers()
    if _CANCELLED.is_set():
        raise KeyboardInterrupt("process execution cancelled")
    located = shutil.which(command[0])
    if located:
        command[0] = str(Path(located).resolve())
    child = None
    streams = [bytearray(), bytearray()]
    limit_reached = threading.Event()
    lock = threading.Lock()
    total = 0
    readers: list[threading.Thread] = []
    read_errors: list[str] = []

    def read(pipe, index):
        nonlocal total
        try:
            read_block = getattr(pipe, "read1", pipe.read)
            while block := read_block(8192):
                with lock:
                    room = max(0, output_limit - total)
                    streams[index].extend(block[:room])
                    total += len(block)
                    if total > output_limit:
                        limit_reached.set()
        except OSError as error:
            read_errors.append(str(error))
        finally:
            pipe.close()

    def kill_tree():
        if os.name == "nt":
            child.kill()
        else:
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass

    failure = None
    status = None
    try:
        environment = dict(os.environ if env is None else env)
        if os.name == "nt":
            with WINDOWS_CREATION_LOCK:
                child = WindowsChild(command, cwd, environment)
        else:
            child = subprocess.Popen(command, cwd=cwd, env=environment, stdin=subprocess.DEVNULL,
                                     stdout=subprocess.PIPE, stderr=subprocess.PIPE, start_new_session=True)
        for index, pipe in enumerate((child.stdout, child.stderr)):
            thread = threading.Thread(target=read, args=(pipe, index), daemon=True)
            thread.start()
            readers.append(thread)
        while True:
            if _CANCELLED.is_set():
                failure = "cancelled"
                break
            if limit_reached.is_set():
                failure = "output_limit"
                break
            if time.monotonic() - started >= timeout:
                failure = "timeout"
                break
            status = child.poll()
            if status is not None:
                break
            time.sleep(0.01)
    except OSError as error:
        failure = ("start_error: " if child is None else "process_error: ") + str(error)
    finally:
        if child is not None:
            try:
                kill_tree()  # also remove descendants after an early parent exit
                if os.name == "nt":
                    waited = child.api.WaitForSingleObject(child.process, 5000)
                    if waited != 0:
                        raise OSError(f"process cleanup wait failed: {waited}")
                    if status is None:
                        status = child.poll()
                else:
                    child.wait(timeout=5)
                    if status is None:
                        status = child.returncode
            except (OSError, subprocess.TimeoutExpired) as error:
                failure = failure or "process_cleanup_error: " + str(error)
            finally:
                if os.name == "nt":
                    child.close()  # closing the Job also kills surviving members
            for reader in readers:
                reader.join(timeout=5)
            if any(reader.is_alive() for reader in readers):
                failure = failure or "pipe_cleanup_failure"
    if _CANCELLED.is_set():
        failure = "cancelled"
    if limit_reached.is_set():
        failure = "output_limit"
    if read_errors:
        failure = failure or "pipe_read_error: " + "; ".join(read_errors)
    if failure is None and status is not None:
        if (os.name == "nt" and status >= 0x80000000) or (os.name != "nt" and status < 0):
            failure = "abnormal_termination"
    return Result(command, str(cwd), status, time.monotonic() - started,
                  bytes(streams[0]), bytes(streams[1]), failure)
