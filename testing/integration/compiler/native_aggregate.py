# SPDX-License-Identifier: MIT OR Apache-2.0
"""One CTest case: independently lay out an argument/result, then call real code.

This model deliberately does not import compiler types, Numeric or layout data.
The bridge uses byte arrays and memcpy, never host C struct layout or __int128.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "support"))
from process import run


SCALARS = {"i8": (1, 1), "u8": (1, 1), "bool": (1, 1),
           "i16": (2, 2), "u16": (2, 2), "i32": (4, 4), "u32": (4, 4),
           "i64": (8, 8), "u64": (8, 8), "isize": (8, 8), "usize": (8, 8),
           "i128": (16, 16), "u128": (16, 16), "f32": (4, 4), "f64": (8, 8), "()": (0, 1)}


def align(value, alignment):
    return (value + alignment - 1) // alignment * alignment


def layout(type_name, definitions):
    if type_name in SCALARS:
        size, alignment = SCALARS[type_name]
        return {"size": size, "alignment": alignment}
    record = definitions[type_name]
    if record["kind"] == "array":
        element = layout(record["element"], definitions)
        return {"size": element["size"] * record["length"], "alignment": element["alignment"]}
    if record["kind"] == "enum":
        count = len(record["variants"])
        tag = next(width for width in (1, 2, 4, 8) if count <= 1 << (8 * width))
        payloads = [product(fields, definitions) for fields in record["variants"]]
        payload_alignment = max((p["alignment"] for p in payloads), default=1)
        alignment = max(tag, payload_alignment)
        offset = align(tag, payload_alignment)
        return {"size": align(offset + max((p["size"] for p in payloads), default=0), alignment),
                "alignment": alignment, "tag": tag, "payload": offset, "variants": payloads}
    return product(record["fields"], definitions)


def product(fields, definitions):
    size, alignment, offsets = 0, 1, []
    for field in fields:
        child = layout(field, definitions)
        size = align(size, child["alignment"])
        offsets.append(size)
        size += child["size"]
        alignment = max(alignment, child["alignment"])
    return {"size": align(size, alignment), "alignment": alignment, "offsets": offsets}


def leaves(type_name, value, definitions, base=0):
    """Logical wire order paired with independently computed object offsets."""
    if type_name in SCALARS:
        size = SCALARS[type_name][0]
        return [] if size == 0 else [(base, size, int(value), type_name)]
    record, shape = definitions[type_name], layout(type_name, definitions)
    if record["kind"] == "array":
        stride = layout(record["element"], definitions)["size"]
        return [leaf for i, item in enumerate(value)
                for leaf in leaves(record["element"], item, definitions, base + i * stride)]
    if record["kind"] == "enum":
        variant, payload = value
        result = [(base, shape["tag"], variant, "tag")]
        fields = record["variants"][variant]
        field_offsets = shape["variants"][variant]["offsets"]
        base += shape["payload"]
    else:
        result = []
        fields, payload = record["fields"], value
        field_offsets = shape["offsets"]
    for field, item, offset in zip(fields, payload, field_offsets):
        result.extend(leaves(field, item, definitions, base + offset))
    return result


def wire(type_name, value, definitions):
    body = b"".join((integer & ((1 << (size * 8)) - 1)).to_bytes(size, "little")
                    for _, size, integer, _ in leaves(type_name, value, definitions))
    return type_name + ":" + body.hex()


def cases():
    pair = {"Pair": {"kind": "struct", "fields": ["u8", "i128", "f64"]}}
    choice = {"Choice": {"kind": "enum", "variants": [[], ["i128", "bool"], ["()"]]}}
    return {
        "struct_record": ("struct Pair { tag: u8, wide: i128, real: f64 }", pair, ["Pair"], "Pair", "return a;", [[255, (1 << 127) + 7, 0x8000000000000000]], [255, (1 << 127) + 7, 0x8000000000000000]),
        "branch_record": ("struct Pair { tag: u8, wide: i128, real: f64 }", pair, ["bool", "Pair", "Pair"], "Pair", "let selected = if (a) { b } else { c }; return selected;", [0, [1, 11, 0], [2, (1 << 128) - 1, 0x8000000000000000]], [2, (1 << 128) - 1, 0x8000000000000000]),
        "edge_snapshot_false": ("struct Pair { tag: u8, wide: i128, real: f64 }", pair,
                                ["bool", "Pair", "Pair"], "Pair",
                                "return if (a) { c } else { b };",
                                [0, [1, 11, 0], [2, (1 << 128) - 1, 0x8000000000000000]],
                                [1, 11, 0]),
        "edge_snapshot_true": ("struct Pair { tag: u8, wide: i128, real: f64 }", pair,
                               ["bool", "Pair", "Pair"], "Pair",
                               "return if (a) { c } else { b };",
                               [1, [1, 11, 0], [2, (1 << 128) - 1, 0x8000000000000000]],
                               [2, (1 << 128) - 1, 0x8000000000000000]),
        "loop_record": ("struct Pair { tag: u8, wide: i128, real: f64 }", pair, ["Pair"], "Pair", "let selected = outer: loop { loop { break @outer a; } }; return selected;", [[3, (1 << 126) + 31, 0x7ff0000000000000]], [3, (1 << 126) + 31, 0x7ff0000000000000]),
        "tuple_record": ("", {"(i8,u128,f32,())": {"kind": "tuple", "fields": ["i8", "u128", "f32", "()"]}}, ["(i8,u128,f32,())"], "(i8,u128,f32,())", "return a;", [[128, (1 << 127) + 19, 1, 0]], [128, (1 << 127) + 19, 1, 0]),
        "array_record": ("", {"i16[3]": {"kind": "array", "element": "i16", "length": 3}}, ["i16[3]"], "i16[3]", "return a;", [[0x8000, 0, 0x7fff]], [0x8000, 0, 0x7fff]),
        "enum_payload": ("enum Choice { Empty, Value { wide: i128, flag: bool }, Unit { value: () } }", choice, ["Choice"], "Choice", "return a;", [[1, [(1 << 128) - 3, 1]]], [1, [(1 << 128) - 3, 1]]),
        "enum_unit": ("enum Choice { Empty, Value { wide: i128, flag: bool }, Unit { value: () } }", choice, ["Choice"], "Choice", "return a;", [[2, [0]]], [2, [0]]),
        "zero_sized": ("struct Empty { first: (), second: () }", {"Empty": {"kind": "struct", "fields": ["()", "()"]}}, ["Empty"], "Empty", "return a;", [[0, 0]], [0, 0]),
        "wide_projection": ("struct Pair { tag: u8, wide: i128, real: f64 }", pair, ["Pair"], "i128", "return a.wide;", [[5, (1 << 128) - 17, 0x3ff0000000000000]], (1 << 128) - 17),
        "mixed_labels": ("struct Pair { tag: u8, wide: i128, real: f64 }", pair, ["Pair", "Pair"], "Pair", "return select(second: a, first: b);", [[1, 11, 0], [2, 22, 1]], [2, 22, 1]),
        "enum_tag16": ("enum Many { " + ", ".join("V" + str(i) for i in range(257)) + " }", {"Many": {"kind": "enum", "variants": [[] for _ in range(257)]}}, ["Many"], "Many", "return a;", [[256, []]], [256, []]),
        "nested_record": ("enum Choice { Empty, Value { wide: i128, flag: bool }, Unit { value: () } } struct Nested { items: Choice[2], tail: u16 }", {**choice, "Choice[2]": {"kind": "array", "element": "Choice", "length": 2}, "Nested": {"kind": "struct", "fields": ["Choice[2]", "u16"]}}, ["Nested"], "Nested", "return a;", [[[[0, []], [1, [1 << 100, 1]]], 65535]], [[[0, []], [1, [1 << 100, 1]]], 65535]),
    }


def cpp_bridge(definitions, inputs, result_type, values, expected):
    lines = ["#include <Chtholly/Runtime/abi.h>", "#include <array>", "#include <bit>",
             "#include <cstdint>", "#include <cstring>", "#include <iostream>",
             "#include <string>", "#include <string_view>", "#include <vector>",
             "using Bits = Chtholly::Runtime::Bits;",
             "auto decode(std::string text, std::string_view type) -> std::vector<unsigned char> {",
             "  const std::string prefix = std::string(type) + ':';",
             "  if (!text.starts_with(prefix) || (text.size() - prefix.size()) % 2 != 0) { throw 2; }",
             "  std::vector<unsigned char> bytes;",
             "  const auto digit = [](char c) { if (c >= '0' && c <= '9') { return c - '0'; } if (c >= 'a' && c <= 'f') { return c - 'a' + 10; } throw 2; };",
             "  for (auto i = prefix.size(); i < text.size(); i += 2) { bytes.push_back(static_cast<unsigned char>(digit(text[i]) * 16 + digit(text[i + 1]))); }",
             "  return bytes;", "}"]
    parameters, arguments, prepare = [], [], []
    result_shape = layout(result_type, definitions)
    indirect_result = result_type not in SCALARS and result_shape["size"] != 0
    wide_result = result_type in ("i128", "u128")
    if indirect_result or wide_result:
        parameters.append("void *")
        arguments.append("output.data()")
    for i, (type_name, value) in enumerate(zip(inputs, values)):
        shape = layout(type_name, definitions)
        prepare += [f"auto wire{i} = decode(argv[{i + 1}], {json.dumps(type_name)});",
                    f"alignas({shape['alignment']}) std::array<unsigned char, {max(shape['size'], 1)}> input{i}{{}};"]
        items = leaves(type_name, value, definitions)
        prepare.append(f"if (wire{i}.size() != {sum(item[1] for item in items)}) {{ return 2; }}")
        cursor = 0
        for offset, size, integer, category in items:
            if category in ("tag", "bool"):
                prepare.append(f"if (wire{i}[{cursor}] != {integer & 255}) {{ return 2; }}")
            prepare.append(f"std::memcpy(input{i}.data() + {offset}, wire{i}.data() + {cursor}, {size});")
            cursor += size
        if shape["size"] == 0:
            continue
        if type_name not in SCALARS:
            parameters.append("const void *")
            arguments.append(f"input{i}.data()")
        elif type_name in ("i128", "u128"):
            prepare.append(f"Bits bits{i}{{}}; std::memcpy(&bits{i}, input{i}.data(), 16);")
            parameters += ["std::uint64_t", "std::uint64_t"]
            arguments += [f"bits{i}.Low", f"bits{i}.High"]
        else:
            native = "float" if type_name == "f32" else "double" if type_name == "f64" else f"std::{'int' if type_name.startswith('i') else 'uint'}{shape['size'] * 8}_t"
            abi = native if shape["size"] >= 4 or type_name == "bool" else "std::int32_t" if type_name.startswith("i") else "std::uint32_t"
            prepare.append(f"{native} value{i}{{}}; std::memcpy(&value{i}, input{i}.data(), {shape['size']});")
            parameters.append(abi)
            arguments.append(f"static_cast<{abi}>(value{i})")
    native_result = "void"
    if result_shape["size"] and not indirect_result and not wide_result:
        native_result = "float" if result_type == "f32" else "double" if result_type == "f64" else f"std::{'int' if result_type.startswith('i') else 'uint'}{max(result_shape['size'] * 8, 8 if result_type == 'bool' else 32)}_t"
    lines += [f'extern "C" auto __chtholly_fn_0({", ".join(parameters)}) -> {native_result};',
              "auto main(int argc, char **argv) -> int {", "try {", f"if (argc != {len(inputs) + 1}) {{ return 2; }}",
              *prepare, f"alignas({result_shape['alignment']}) std::array<unsigned char, {max(result_shape['size'], 1)}> output{{}};",
              "Chtholly::Runtime::chtholly_runtime_abi_v3();", "Chtholly::Runtime::chtholly_runtime_initialize();"]
    call = f'__chtholly_fn_0({", ".join(arguments)})'
    lines += [call + ";"] if native_result == "void" else [f"auto returned = {call};", f"std::memcpy(output.data(), &returned, {result_shape['size']});"]
    lines += [f"std::cout << {json.dumps(result_type + ':')};", 'constexpr char digits[] = "0123456789abcdef";']
    for offset, size, integer, category in leaves(result_type, expected, definitions):
        if category == "tag":
            lines.append(f"if (output[{offset}] != {integer & 255}) {{ return 3; }}")
        lines.append(f"for (unsigned i = 0; i < {size}; ++i) {{ const auto byte = output[{offset} + i]; std::cout << digits[byte >> 4] << digits[byte & 15]; }}")
    lines += ["std::cout << '\\n'; return 0;", "} catch (...) { return 2; }", "}"]
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    for name in ("fixture", "clang", "runtime", "root", "work-dir"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--case", choices=tuple(cases()), required=True)
    args = parser.parse_args()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="aggregate-" + args.case + "-", dir=args.work_dir)).resolve()
    report_root = Path(os.environ.get("CHTHOLLY_REPORT_DIR", str(work / "reports")))
    report_root.mkdir(parents=True, exist_ok=True)
    report = Path(tempfile.mkdtemp(prefix="aggregate-", dir=report_root))
    record = {"case": args.case, "basis": "R3; independent target layout arithmetic and complete logical field bit vectors",
              "steps": [], "status": "not_executed",
              "stages": {stage: {"executed": False, "reason": "not reached"}
                         for stage in ("object", "bridge", "run")}}

    def execute(arguments, timeout=30):
        observed = run([str(item) for item in arguments], work, timeout)
        index = len(record["steps"])
        for stream in ("stdout", "stderr"):
            (report / f"{index}.{stream}.txt").write_bytes(getattr(observed, stream))
        record["steps"].append({"argv": observed.argv, "returncode": observed.returncode,
                                "failure": observed.failure, "duration": observed.duration})
        stage = ("object", "bridge", "run")[index]
        record["stages"][stage] = dict(record["steps"][-1], executed=True)
        if observed.failure:
            if observed.failure.startswith(("start_error", "process_error", "process_cleanup_error", "pipe_", "cancelled")):
                raise OSError(observed.failure)
            raise ValueError(observed.failure)
        if any(marker in observed.stderr for marker in (b"AddressSanitizer", b"UndefinedBehaviorSanitizer", b"runtime error:")):
            raise ValueError("sanitizer report")
        return observed

    try:
        declarations, definitions, inputs, result_type, body, values, expected = cases()[args.case]
        parameters = ", ".join(f"_ {chr(97 + i)}: {kind}" for i, kind in enumerate(inputs))
        extra = "fn select(first: Pair, second: Pair) -> Pair { return first; }" if args.case == "mixed_labels" else ""
        text = f"{declarations}\nfn probe({parameters}) -> {result_type} {{ {body} }}\n{extra}\nfn main() -> i32 {{ return 0; }}\n"
        source, object_path, executable = work / "input.cns", work / "input.obj", work / "probe.exe"
        source.write_text(text, encoding="utf-8")
        (report / "input.cns").write_text(text, encoding="utf-8")
        explicit_ir = args.case.startswith("edge_snapshot_")
        record.update(source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                      source_form="explicit IR fixture; input.cns is an equivalent example" if explicit_ir else "compiled Chtholly source",
                      runtime_sha256=hashlib.sha256(args.runtime.read_bytes()).hexdigest(),
                      runtime_metadata=(args.runtime.parent / "chtholly-runtime.meta").read_text(encoding="utf-8"),
                      types=definitions, inputs=[wire(t, v, definitions) for t, v in zip(inputs, values)],
                      expected=wire(result_type, expected, definitions))
        if explicit_ir:
            fixture_source = args.root / "testing/integration/compiler/aggregate_ir_fixture.h"
            fixture_bytes = fixture_source.read_bytes()
            (report / "aggregate_ir_fixture.h").write_bytes(fixture_bytes)
            record["ir_fixture"] = {"path": str(fixture_source),
                                    "sha256": hashlib.sha256(fixture_bytes).hexdigest()}
        object_input = "--aggregate-edge" if explicit_ir else source
        if execute([args.fixture, object_input, object_path]).returncode != 0 or not object_path.is_file() or object_path.stat().st_size <= 20:
            raise ValueError("object emission failed")
        # Native values are supplied only after the source object is complete.
        bridge = cpp_bridge(definitions, inputs, result_type, values, expected)
        bridge_path = work / "bridge.cpp"
        bridge_path.write_text(bridge, encoding="utf-8")
        (report / "bridge.cpp").write_text(bridge, encoding="utf-8")
        command = [args.clang, "/nologo", "/std:c++20", "/MD", "/EHsc", "/utf-8", "/D_ITERATOR_DEBUG_LEVEL=0",
                   "/I" + str(args.root / "toolchain/runtime/include"), bridge_path, object_path,
                   args.runtime, "/Fe" + str(executable), "-fuse-ld=lld", "/link", "/incremental:no"]
        if execute(command).returncode != 0 or not executable.is_file():
            raise ValueError("ABI bridge link failed")
        observed = execute([executable, *record["inputs"]], 5)
        if observed.returncode != 0 or observed.stderr or observed.stdout.replace(b"\r\n", b"\n") != (record["expected"] + "\n").encode("ascii"):
            raise ValueError("native logical field bit pattern mismatch")
        record["status"] = "passed"
        return 0
    except OSError as error:
        record.update(status="infrastructure_error", error=str(error))
        print(error, file=sys.stderr)
        return 2
    except (ValueError, KeyError, IndexError) as error:
        record.update(status="failed", error=str(error))
        print(error, file=sys.stderr)
        return 1
    finally:
        try:
            (report / "result.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
        except OSError as error:
            print("cannot save aggregate report: " + str(error), file=sys.stderr)
            return 2


if __name__ == "__main__":
    sys.exit(main())
