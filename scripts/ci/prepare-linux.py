# SPDX-License-Identifier: MIT OR Apache-2.0
"""Explicit CI dependency preparation, outside CMake and test execution."""
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import urllib.request
import uuid
import zipfile
import os


def main():
    root = Path(__file__).resolve().parents[2]
    lock = json.loads((root / "cmake/dependencies.json").read_text(encoding="utf-8"))
    cache = root / "out/dependencies"
    cache.mkdir(parents=True, exist_ok=True)
    extraction = cache / ("extract-" + uuid.uuid4().hex)
    extraction.mkdir()
    for name, filename, url in (
        ("cmake", "cmake-3.31.12-linux-x86_64.tar.gz", "https://github.com/Kitware/CMake/releases/download/v3.31.12/"),
        ("ninja", "ninja-linux.zip", "https://github.com/ninja-build/ninja/releases/download/v1.13.2/"),
    ):
        archive = cache / filename
        if not archive.exists():
            urllib.request.urlretrieve(url + filename, archive)
        if hashlib.sha256(archive.read_bytes()).hexdigest() != lock[name]["linux_sha256"]:
            raise ValueError("dependency checksum mismatch: " + filename)
        if name == "cmake":
            with tarfile.open(archive) as content:
                # Python 3.11.9 includes the explicit safe extraction filter.
                content.extractall(extraction, filter="data")
            binary = extraction / "cmake-3.31.12-linux-x86_64/bin"
        else:
            binary = extraction / "ninja"
            with zipfile.ZipFile(archive) as content:
                if content.namelist() != ["ninja"]:
                    raise ValueError("unexpected Ninja archive members")
                content.extractall(binary)
            (binary / "ninja").chmod(0o755)
        with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8") as output:
            output.write(str(binary) + "\n")
    gtest = extraction / "googletest"
    subprocess.run(["git", "clone", "--no-checkout", lock["googletest"]["repository"], str(gtest)], check=True)
    subprocess.run(["git", "-C", str(gtest), "checkout", "--detach", lock["googletest"]["commit"]], check=True)
    with open(os.environ["GITHUB_ENV"], "a", encoding="utf-8") as output:
        output.write("CHTHOLLY_GTEST_SOURCE_DIR=" + str(gtest) + "\n")


if __name__ == "__main__":
    main()
