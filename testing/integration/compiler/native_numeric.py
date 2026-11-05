# SPDX-License-Identifier: MIT OR Apache-2.0
"""One CTest-selected backend case with inputs supplied after object emission."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "support"))
from process import run


WIDTHS = {"i8": 8, "i16": 16, "i32": 32, "i64": 64, "i128": 128,
          "u8": 8, "u16": 16, "u32": 32, "u64": 64, "u128": 128,
          "isize": 64, "usize": 64, "f32": 32, "f64": 64, "bool": 8, "()": 0}
INTEGER_TYPES = tuple(name for name in WIDTHS if name.startswith(("i", "u")))


def signed(type_name):
    return type_name.startswith("i")


def raw(type_name, value):
    width = WIDTHS[type_name]
    return type_name + ":" + (int(value) & ((1 << width) - 1)).to_bytes(width // 8, "little").hex()


def vectors():
    result = {}
    for type_name in INTEGER_TYPES:
        width = WIDTHS[type_name]
        maximum = (1 << (width - int(signed(type_name)))) - 1
        minimum = -(1 << (width - 1)) if signed(type_name) else 0
        result[type_name + "_add"] = {"types": [type_name, type_name], "result": type_name,
            "expression": "left + right", "inputs": [maximum - 1, 1], "expected": maximum}
        result[type_name + "_overflow"] = {"types": [type_name, type_name], "result": type_name,
            "expression": "left + right", "inputs": [maximum, 1], "failure": "Arithmetic.Overflow", "operator": "+"}
        result[type_name + "_divide"] = {"types": [type_name, type_name], "result": type_name,
            "expression": "left / right", "inputs": [minimum if signed(type_name) else maximum, 3],
            "expected": -(abs(minimum) // 3) if signed(type_name) else maximum // 3}
        result[type_name + "_remainder"] = {"types": [type_name, type_name], "result": type_name,
            "expression": "left % right", "inputs": [minimum if signed(type_name) else maximum, -1 if signed(type_name) else 3],
            "expected": 0 if signed(type_name) else maximum % 3}
        result[type_name + "_shift"] = {"types": [type_name, "u128"], "result": type_name,
            "expression": "left << right", "inputs": [maximum, 1], "expected": (maximum << 1) & ((1 << width) - 1)}
        result[type_name + "_multiply"] = {"types": [type_name, type_name], "result": type_name,
            "expression": "left * right", "inputs": [maximum // 3, 3], "expected": maximum // 3 * 3}
        result[type_name + "_multiply_overflow"] = {"types": [type_name, type_name], "result": type_name,
            "expression": "left * right", "inputs": [maximum, 2], "failure": "Arithmetic.Overflow", "operator": "*"}
        result[type_name + "_subtract"] = {"types": [type_name, type_name], "result": type_name,
            "expression": "left - right", "inputs": [maximum, 1], "expected": maximum - 1}
        result[type_name + "_divide_zero"] = {"types": [type_name, type_name], "result": type_name,
            "expression": "left / right", "inputs": [maximum, 0], "failure": "Arithmetic.DivisionByZero", "operator": "/"}
        result[type_name + "_shift_count"] = {"types": [type_name, "u128"], "result": type_name,
            "expression": "left << right", "inputs": [1, width], "failure": "Arithmetic.ShiftCount", "operator": "<<"}
        result[type_name + "_right_shift"] = {"types": [type_name, "i32"], "result": type_name,
            "expression": "left >> right", "inputs": [minimum if signed(type_name) else maximum, 1],
            "expected": minimum >> 1 if signed(type_name) else maximum >> 1}
        result[type_name + "_bits"] = {"types": [type_name, type_name], "result": type_name,
            "expression": "(~left & right) ^ (left | right)", "inputs": [42, 15], "expected": ((~42 & 15) ^ (42 | 15)) & ((1 << width) - 1)}
        result[type_name + "_negate"] = {"types": [type_name], "result": type_name,
            "expression": "-left", "inputs": [maximum if signed(type_name) else 0],
            "expected": -maximum if signed(type_name) else 0}
        result[type_name + "_negate_fail"] = {"types": [type_name], "result": type_name,
            "expression": "-left", "inputs": [minimum if signed(type_name) else 1], "failure": "Arithmetic.Overflow", "operator": "-"}
        result[type_name + "_compare"] = {"types": [type_name, type_name], "result": "bool",
            "expression": "left < right", "inputs": [minimum if signed(type_name) else maximum, 0], "expected": int(signed(type_name))}
        if signed(type_name):
            result[type_name + "_divide_min"] = {"types": [type_name, type_name], "result": type_name,
                "expression": "left / right", "inputs": [minimum, -1], "failure": "Arithmetic.Overflow", "operator": "/"}
    numerical = INTEGER_TYPES + ("f32", "f64")
    for source in numerical:
        for destination in numerical:
            result["convert_" + source + "_" + destination] = {
                "types": [source], "result": destination, "expression": "left as " + destination,
                "inputs": [0x3f800000 if source == "f32" else 0x3ff0000000000000 if source == "f64" else 1],
                "expected": 0x3f800000 if destination == "f32" else 0x3ff0000000000000 if destination == "f64" else 1}
    for type_name, one, half, one_half, two, three, maximum, infinity, negative_zero in (
            ("f32", 0x3f800000, 0x3f000000, 0x3fc00000, 0x40000000, 0x40400000, 0x7f7fffff, 0x7f800000, 0x80000000),
            ("f64", 0x3ff0000000000000, 0x3fe0000000000000, 0x3ff8000000000000, 0x4000000000000000,
             0x4008000000000000, 0x7fefffffffffffff, 0x7ff0000000000000, 0x8000000000000000)):
        for operation, expression, inputs, expected in (
                ("subtract", "left - right", [one, half], half),
                ("multiply", "left * right", [one_half, two], three),
                ("divide", "left / right", [one, two], half),
                ("negate", "-left", [0], negative_zero),
                ("overflow", "left + right", [maximum, maximum], infinity)):
            result[type_name + "_" + operation] = {"types": [type_name] * len(inputs),
                "result": type_name, "expression": expression, "inputs": inputs, "expected": expected}
    result.update({
        "f32_add": {"types": ["f32", "f32"], "result": "f32", "expression": "left + right",
                    "inputs": [0x3f800000, 0x33800000], "expected": 0x3f800000},
        "f64_add": {"types": ["f64", "f64"], "result": "f64", "expression": "left + right",
                    "inputs": [0x3ff0000000000000, 0x3ca0000000000000], "expected": 0x3ff0000000000000},
        "subnormal": {"types": ["f32", "f32"], "result": "f32", "expression": "left + right", "inputs": [1, 1], "expected": 2},
        "negative_zero": {"types": ["f64"], "result": "f32", "expression": "left as f32", "inputs": [0x8000000000000000], "expected": 0x80000000},
        "infinity": {"types": ["f32", "f32"], "result": "f32", "expression": "left / right", "inputs": [0x3f800000, 0], "expected": 0x7f800000},
        "nan": {"types": ["f64", "f64"], "result": "f64", "expression": "left / right", "inputs": [0, 0], "nan": True},
        "float_range": {"types": ["f64"], "result": "f32", "expression": "left as f32", "inputs": [0x47efffffe0000001], "failure": "Conversion.OutOfRange", "operator": "as"},
        "integer_float_range": {"types": ["u128"], "result": "f32", "expression": "left as f32",
                                "inputs": [(1 << 128) - (1 << 104) + 1], "failure": "Conversion.OutOfRange", "operator": "as"},
        "integer_float_precision": {"types": ["i32"], "result": "f32", "expression": "left as f32", "inputs": [16777217], "failure": "Conversion.PrecisionLoss", "operator": "as"},
        "float_nonfinite": {"types": ["f64"], "result": "i128", "expression": "left as i128", "inputs": [0x7ff0000000000000], "failure": "Conversion.NonFinite", "operator": "as"},
        "float_fractional": {"types": ["f64"], "result": "u8", "expression": "left as u8", "inputs": [0x4070080000000000], "failure": "Conversion.Fractional", "operator": "as"},
        "float_integer": {"types": ["f64"], "result": "i128", "expression": "left as i128", "inputs": [0xc3e0000000000000], "expected": -(1 << 63)},
        "integer_float": {"types": ["u128"], "result": "f64", "expression": "left as f64", "inputs": [1 << 100], "expected": 0x4630000000000000},
        "bool_identity": {"types": ["bool"], "result": "bool", "expression": "!left", "inputs": [0], "expected": 1},
        "unit_argument": {"types": ["()", "i32"], "result": "i32", "expression": "right", "inputs": [0, 42], "expected": 42},
        "unit_result": {"types": ["i32"], "result": "()", "expression": "()", "inputs": [42], "expected": 0},
        "root_replacement": {"types": ["i32"], "result": "u8", "body": "let value = left as u8 else { 7 }; return value;", "inputs": [300], "expected": 7},
        "short_circuit": {"types": ["bool"], "result": "bool", "expression": "left && { abort(); }", "inputs": [0], "expected": 0},
        "argument_order": {"types": ["i32"], "result": "i32", "body": "var x = left; return combine(second: { x = x + 1; x }, first: { x = x + 1; x });",
                           "extra": "fn combine(first: i32, second: i32) -> i32 { return first * 10 + second; }", "inputs": [0], "expected": 21},
        "recovery_side_effect": {"types": ["i32"], "result": "i32", "body": "var count = left; let value = { count = count + 1; count } as u8 else { count = count + 10; 0 }; return count;", "inputs": [300], "expected": 311},
        "recovery_lazy": {"types": ["i32"], "result": "i32", "body": "var count = left; let value = { count = count + 1; count } as u8 else { count = count + 10; 0 }; return count;", "inputs": [0], "expected": 1},
        "recovery_outer_name": {"types": ["i32"], "result": "u8", "body": "let value = left; { let value = value as u8 else { if (value == 300) { 7 } else { 8 } }; return value; }", "inputs": [300], "expected": 7},
        "rhs_order": {"types": ["i32"], "result": "i32", "body": "var x = left; x = { x = x + 1; x + 1 }; return x;", "inputs": [40], "expected": 42},
        "mutual_recursion": {"types": ["i32"], "result": "bool", "expression": "even(left)",
            "extra": "fn even(x: i32) -> bool { if (x == 0) { return true; } return odd(x - 1); } fn odd(x: i32) -> bool { if (x == 0) { return false; } return even(x - 1); }", "inputs": [8], "expected": 1},
        "assert_once": {"types": ["i32"], "result": "i32", "body": "var count = left; assert({ count = count + 1; count == 42 }); return count;", "inputs": [41], "expected": 42},
        "recovery_child_failure": {"types": ["i32"], "result": "u8", "body": "let value = (1 / left) as u8 else { 0 }; return value;", "inputs": [0], "failure": "Arithmetic.DivisionByZero", "operator": "/"},
        "recovery_own_failure": {"types": ["i32"], "result": "u8", "body": "let zero = 0u8; let value = left as u8 else { 1u8 / zero }; return value;", "inputs": [300], "failure": "Arithmetic.DivisionByZero", "operator": "/"},
        "negative_float_range": {"types": ["f64"], "result": "f32", "expression": "left as f32", "inputs": [0xc7efffffe0000001], "failure": "Conversion.OutOfRange", "operator": "as"},
        "negative_shift_count": {"types": ["i128", "i128"], "result": "i128", "expression": "left >> right", "inputs": [-1, -1], "failure": "Arithmetic.ShiftCount", "operator": ">>"},
        "nan_inequality": {"types": ["f64", "f64"], "result": "bool", "expression": "left != right", "inputs": [0x7ff8000000000001, 0x7ff8000000000001], "expected": 1},
        "nan_ordering": {"types": ["f64", "f64"], "result": "bool", "expression": "left < right", "inputs": [0x7ff8000000000001, 0], "expected": 0},
        "zero_equality": {"types": ["f32", "f32"], "result": "bool", "expression": "left == right", "inputs": [0x80000000, 0], "expected": 1},
        "var_parameter": {"types": ["i32"], "result": "i32", "body": "let changed = increment(left); return changed - left;",
                          "extra": "fn increment(var value: i32) -> i32 { value = value + 1; return value; }", "inputs": [41], "expected": 1},
    })
    return result


def abi_type(type_name):
    if type_name in ("f32", "f64"):
        return "float" if type_name == "f32" else "double"
    width = max(32, WIDTHS[type_name]) if type_name != "bool" else 8
    return ("std::int" if signed(type_name) else "std::uint") + str(width) + "_t"


def bridge(case):
    parameters, arguments = [], []
    wide_result = WIDTHS[case["result"]] == 128
    if wide_result:
        parameters.append("Bits *")
        arguments.append("&output")
    for index, type_name in enumerate(case["types"]):
        width = WIDTHS[type_name]
        value = f"inputs[{index}].Low"
        if width == 0:
            continue
        if width == 128:
            parameters += ["std::uint64_t", "std::uint64_t"]
            arguments += [value, f"inputs[{index}].High"]
        elif type_name in ("f32", "f64"):
            parameters.append(abi_type(type_name))
            arguments.append(f"std::bit_cast<{abi_type(type_name)}>(static_cast<std::uint{width}_t>({value}))")
        else:
            parameters.append(abi_type(type_name))
            if signed(type_name):
                value = f"std::bit_cast<std::int{width}_t>(static_cast<std::uint{width}_t>({value}))"
            arguments.append(f"static_cast<{abi_type(type_name)}>({value})")
    result_type = "void" if wide_result or case["result"] == "()" else abi_type(case["result"])
    lines = [f'extern "C" auto __chtholly_fn_0({", ".join(parameters)}) -> {result_type};',
             f'inline constexpr std::array<std::string_view, {len(case["types"])}> InputTypes = {{{", ".join(json.dumps(t) for t in case["types"])}}};',
             f'inline constexpr std::array<unsigned, {len(case["types"])}> InputBytes = {{{", ".join(str(WIDTHS[t] // 8) for t in case["types"])}}};',
             f'inline constexpr std::string_view OutputType = {json.dumps(case["result"])};',
             f'inline constexpr unsigned OutputBytes = {WIDTHS[case["result"]] // 8};',
             "auto BridgeInvoke(std::span<const Bits> inputs) -> Bits {", "  Bits output{};"]
    call = f'__chtholly_fn_0({", ".join(arguments)})'
    if result_type == "void":
        lines.append("  " + call + ";")
    elif case["result"] in ("f32", "f64"):
        lines.append(f'  output.Low = std::bit_cast<std::uint{WIDTHS[case["result"]]}_t>({call});')
    else:
        lines.append("  output.Low = static_cast<std::uint64_t>(" + call + ");")
    lines += ["  return output;", "}"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    for name in ("fixture", "clang", "runtime", "root", "work-dir"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--case", required=True, choices=tuple(vectors()))
    args = parser.parse_args()
    case = vectors()[args.case]
    args.work_dir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix=args.case + "-", dir=args.work_dir)).resolve()
    report_root = Path(os.environ.get("CHTHOLLY_REPORT_DIR", str(work / "reports")))
    report_root.mkdir(parents=True, exist_ok=True)
    report = Path(tempfile.mkdtemp(prefix="native-" + args.case + "-", dir=report_root))
    records = {"case": args.case, "basis": "independent Python integer model and IEEE bit vectors",
               "rules": "R2/B2 plus the uninhabited-parameter normal-entry decision",
               "steps": [], "stages": {name: {"executed": False, "reason": "not reached"}
                    for name in ("emit_object", "link_bridge", "execute")},
               "not_executed": ["product CLI is covered by separate e2e cases"]}
    def execute(command, timeout=30):
        stage = ("emit_object", "link_bridge", "execute")[len(records["steps"])]
        records["stages"][stage] = {"executed": True, "status": "interrupted_or_incomplete"}
        observed = run(list(map(str, command)), work, timeout=timeout)
        index = len(records["steps"])
        for stream in ("stdout", "stderr"):
            (report / f"{index}.{stream}.txt").write_bytes(getattr(observed, stream))
        records["steps"].append({"argv": observed.argv, "returncode": observed.returncode,
                                 "failure": observed.failure, "duration": observed.duration})
        records["stages"][stage] = dict(records["steps"][-1], executed=True)
        if observed.failure and observed.failure.startswith(("start_error", "process_error", "process_cleanup_error", "pipe_", "cancelled")):
            raise OSError("process infrastructure failure: " + observed.failure)
        if observed.failure or any(marker in observed.stderr for marker in (b"AddressSanitizer", b"UndefinedBehaviorSanitizer", b"runtime error:")):
            raise ValueError("process or sanitizer failure")
        return observed
    try:
        records["runtime_sha256"] = hashlib.sha256(args.runtime.read_bytes()).hexdigest()
        records["runtime_metadata"] = (args.runtime.parent / "chtholly-runtime.meta").read_text(encoding="utf-8")
        names = ("left", "right")
        parameters = ", ".join(f"_ {names[i]}: {type_name}" for i, type_name in enumerate(case["types"]))
        body = case.get("body", "return " + case.get("expression", "()") + ";")
        text = f'fn probe({parameters}) -> {case["result"]} {{ {body} }}\n' + case.get("extra", "") + "\nfn main() -> i32 { return 0; }\n"
        source, object_file = work / "input.cns", work / "probe.obj"
        source.write_text(text, encoding="utf-8")
        (report / "input.cns").write_text(text, encoding="utf-8")
        records["source_sha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
        if execute([args.fixture, source, object_file]).returncode != 0 or not object_file.is_file():
            raise ValueError("language object emission failed")
        # The program object is complete before input values enter any process.
        (work / "bridge_config.h").write_text(bridge(case), encoding="utf-8")
        (report / "bridge_config.h").write_text(bridge(case), encoding="utf-8")
        executable = work / "probe.exe"
        command = [args.clang, "/nologo", "/std:c++20", "/MD", "/EHsc", "/utf-8", "/D_ITERATOR_DEBUG_LEVEL=0",
                   "/I" + str(work), "/I" + str(args.root / "toolchain/runtime/include"),
                   args.root / "testing/support/fixtures/native_bridge.cpp", object_file, args.runtime,
                   "/Fe" + str(executable), "-fuse-ld=lld", "/link", "/incremental:no"]
        if execute(command).returncode != 0 or not executable.is_file():
            raise ValueError("test ABI bridge link failed")
        observed = execute([executable, *(raw(t, v) for t, v in zip(case["types"], case["inputs"]))], timeout=5)
        records["inputs"] = [raw(t, v) for t, v in zip(case["types"], case["inputs"])]
        records["expectation"] = {key: case[key] for key in ("expected", "nan", "failure") if key in case}
        if "failure" in case:
            column = text.index(case["operator"], text.index("{")) + 1
            expected = f'{source}:1:{column}: chtholly failure[{case["failure"]}]\n'.encode("utf-8")
            if observed.returncode != 120 or observed.stdout or observed.stderr.replace(b"\r\n", b"\n") != expected:
                raise ValueError("language failure category, position, or status mismatch")
        else:
            if observed.returncode != 0 or observed.stderr:
                raise ValueError("native execution failed")
            output = observed.stdout.decode("ascii").replace("\r\n", "\n")
            wire_pattern = re.escape(case["result"]) + ":[0-9a-f]{" + str(WIDTHS[case["result"]] // 4) + "}\n"
            if not re.fullmatch(wire_pattern, output):
                raise ValueError("invalid result type, byte length, or wire syntax")
            output = output[:-1]
            if case.get("nan"):
                prefix = case["result"] + ":"
                if not output.startswith(prefix) or len(output) != len(prefix) + WIDTHS[case["result"]] // 4:
                    raise ValueError("invalid NaN wire result")
                bits = int.from_bytes(bytes.fromhex(output[len(prefix):]), "little")
                if bits & 0x7ff0000000000000 != 0x7ff0000000000000 or bits & 0xfffffffffffff == 0:
                    raise ValueError("result is not an IEEE NaN")
            elif output != raw(case["result"], case["expected"]):
                raise ValueError("complete result bit pattern mismatch")
        records["status"] = "passed"
        return 0
    except OSError as error:
        records.update(status="infrastructure_error", error=str(error))
        print(error, file=sys.stderr)
        return 2
    except ValueError as error:
        records.update(status="failed", error=str(error))
        print(error, file=sys.stderr)
        return 1
    finally:
        try:
            (report / "result.json").write_text(json.dumps(records, indent=2), encoding="utf-8")
        except OSError as error:
            print("cannot save native component report: " + str(error), file=sys.stderr)
            return 2


if __name__ == "__main__":
    sys.exit(main())
