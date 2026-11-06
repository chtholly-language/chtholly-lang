# SPDX-License-Identifier: MIT OR Apache-2.0
"""Read generated properties and File API; never configure or compile."""
import argparse
import json
from pathlib import Path
import re
import sys


def canonical(name):
    if name.startswith("Chtholly::"):
        return "chtholly_" + {"SemIR": "sem_ir", "CodeGen": "codegen"}.get(
            name.split("::")[1], name.split("::")[1].lower())
    return name


def without_link_only(value):
    return re.sub(r"\$<LINK_ONLY:([^<>]*)>", r"\1", value)


def dependencies(value):
    return {canonical(x) for x in without_link_only(value).split(";") if x}


def public_dependencies(value):
    return dependencies(re.sub(r"\$<LINK_ONLY:[^<>]*>", "", value))


def cycles(graph):
    visited, active = set(), set()
    def visit(node):
        if node in active:
            raise ValueError(f"dependency cycle at {node}")
        if node in visited:
            return
        active.add(node)
        for child in graph.get(node, set()):
            if child in graph:
                visit(child)
        active.remove(node)
        visited.add(node)
    for node in graph:
        visit(node)


def scan_includes(root, modules):
    failures = []
    for module in modules:
        for path in (root / "toolchain" / module).rglob("*"):
            if path.suffix not in (".h", ".cpp"):
                continue
            for include in re.findall(r'^\s*#\s*include\s*[<"]([^">]+)', path.read_text(encoding="utf-8"), re.M):
                if ".." in include.replace("\\", "/").split("/"):
                    failures.append(f"{path}: parent-relative include")
                if include.startswith("llvm/"):
                    allowed_headers = modules[module].get("llvm_headers", [])
                    if "include" in path.parts or not any(include.startswith(prefix) for prefix in allowed_headers):
                        failures.append(f"{path}: LLVM include outside declared private component boundary")
                if include.startswith("Chtholly/") and include != "Chtholly/build_config.h":
                    component = include.split("/")[1]
                    target = canonical("Chtholly::" + component).removeprefix("chtholly_")
                    is_public = "include" in path.relative_to(root / "toolchain" / module).parts
                    allowed = {module, *modules[module].get("public" if is_public else "links", [])}
                    if target not in allowed:
                        failures.append(f"{path}: undeclared direct include of {target}")
                    public_file = root / "toolchain" / target / "include" / include
                    if not public_file.is_file():
                        failures.append(f"{path}: include is not a module public header: {include}")
                if "include" in path.relative_to(root / "toolchain" / module).parts and include == "Chtholly/build_config.h":
                    failures.append(f"{path}: generated configuration leaked through public header")
                if include.startswith("toolchain/"):
                    failures.append(f"{path}: cross-module private include")
                normalized = include.replace("\\", "/")
                if normalized.split("/")[0] in modules and normalized.split("/")[0] != module:
                    failures.append(f"{path}: module-relative private include")
                absolute = Path(include)
                if absolute.is_absolute() and absolute.resolve().is_relative_to(root / "toolchain"):
                    failures.append(f"{path}: absolute include bypasses module interface")
    return failures


def inspect_runtime(root, build):
    """The independently compiled target runtime must provide real evidence."""
    runtime_build = build / "target-runtime"
    reply = runtime_build / ".cmake/api/v1/reply"
    indexes = sorted(reply.glob("index-*.json"))
    if not indexes:
        raise ValueError("missing target runtime File API reply")
    index = json.loads(indexes[-1].read_text(encoding="utf-8"))
    objects = {item["kind"]: json.loads((reply / item["jsonFile"]).read_text(encoding="utf-8")) for item in index["objects"]}
    targets = objects["codemodel"]["configurations"][0]["targets"]
    names = {item["id"]: item["name"] for item in targets}
    if set(names.values()) != {"chtholly_target_runtime", "chtholly_target_builtins", "chtholly_runtime_metadata"}:
        raise ValueError("unexpected targets in runtime sub-build")
    cache = {item["name"]: item["value"] for item in objects["cache"]["entries"]}
    manifest_toolchain = root / "cmake/WindowsSDKManifest.cmake"
    if Path(cache.get("CMAKE_TOOLCHAIN_FILE", "")).resolve() != manifest_toolchain.resolve():
        raise ValueError("target runtime did not select the SDK manifest toolchain")
    manifest_tool = Path(cache.get("CMAKE_MT", ""))
    if manifest_tool.name.lower() != "mt.exe" or manifest_tool.parent.name.lower() != "x64":
        raise ValueError("target runtime did not select Windows SDK x64 mt.exe")
    source_root = Path(cache["CHTHOLLY_LLVM_SOURCE_DIR"]).resolve()
    lock = json.loads((root / "cmake/dependencies.json").read_text(encoding="utf-8"))
    expected_builtins = {(source_root / "compiler-rt/lib/builtins" / (name + ".c")).resolve()
                         for name in lock["llvm_source"]["builtins"]}
    properties = dict(line.split("=", 1) for line in (runtime_build / "architecture/runtime.txt").read_text(encoding="utf-8").splitlines() if "=" in line)
    if (properties["compiler"], properties["version"], properties["pointer_bytes"], properties["crt"]) != (
            "Clang", "18.1.8", "8", "MultiThreadedDLL"):
        raise ValueError("target runtime toolchain or CRT mismatch")
    allowed_includes = {(root / "toolchain/runtime/include").resolve(), (root / "toolchain/target/include").resolve(),
                        (root / "toolchain/types/include").resolve()}
    if {Path(item).resolve() for item in properties["includes"].split(";") if item} != allowed_includes:
        raise ValueError("target runtime has undeclared include directories")
    if "sanitize" in properties["options"].lower():
        raise ValueError("target runtime unexpectedly inherits host sanitizer")
    for item in targets:
        target = json.loads((reply / item["jsonFile"]).read_text(encoding="utf-8"))
        sources = {Path(source["path"]).resolve() for source in target.get("sources", []) if "compileGroupIndex" in source}
        if item["name"] == "chtholly_target_builtins":
            expected = expected_builtins
            expected_dependencies = set()
        elif item["name"] == "chtholly_target_runtime":
            expected = {(root / "toolchain/runtime/runtime.cpp").resolve()}
            expected_dependencies = {"chtholly_target_builtins"}
        else:
            expected = set()
            expected_dependencies = {"chtholly_target_runtime"}
        if sources != expected:
            raise ValueError("runtime File API source inventory mismatch: " + item["name"])
        for group in target.get("compileGroups", []):
            if any("sanitize" in fragment["fragment"].lower() for fragment in group.get("compileCommandFragments", [])):
                raise ValueError("runtime compile command contains a sanitizer")
        dependencies = {names[dependency["id"]] for dependency in target.get("dependencies", [])}
        allowed_dependencies = (expected_dependencies | {"chtholly_target_builtins"}
                                if item["name"] == "chtholly_runtime_metadata" else expected_dependencies)
        if not expected_dependencies <= dependencies or not dependencies <= allowed_dependencies:
            raise ValueError("unexpected runtime dependency propagation")
    if any(name.startswith("Python3_") and name.endswith("EXECUTABLE") for name in cache):
        raise ValueError("target runtime discovered Python")


def inspect(root, build):
    lock = json.loads((root / "cmake/module-dependencies.json").read_text(encoding="utf-8"))
    failures = scan_includes(root, lock["modules"])
    exported = {}
    for file in (build / "architecture").glob("*.txt"):
        props = dict(line.split("=", 1) for line in file.read_text(encoding="utf-8").splitlines() if "=" in line)
        exported[props["name"]] = props
    if not exported:
        raise ValueError("missing generated target properties; configure first")
    specifications = dict(lock["targets"])
    for module, spec in lock["modules"].items():
        specifications["chtholly_" + module] = {
            "links": ["chtholly_" + dep for dep in spec["links"]],
            "public": ["chtholly_" + dep for dep in spec["public"]],
            "build": spec.get("build", []),
            "llvm_libraries": spec.get("llvm_libraries", [])}
    graph = {}
    for name, props in exported.items():
        if name == "chtholly_public_headers":
            continue
        if name not in specifications:
            failures.append(f"unregistered project target: {name}")
            continue
        spec = specifications[name]
        links = dependencies(props["links"])
        internal = {dep for dep in links if dep.startswith(("chtholly", "GTest::"))}
        if internal != set(spec["links"]):
            failures.append(f"{name}: direct dependencies differ: {internal} != {spec['links']}")
        public = public_dependencies(props["interface_links"])
        if public != set(spec["public"]):
            failures.append(f"{name}: public dependencies differ: {public}")
        llvm_dependencies = {dep for dep in links if dep.startswith("LLVM")}
        if llvm_dependencies != set(spec.get("llvm_libraries", [])):
            failures.append(f"{name}: LLVM component dependencies differ: {llvm_dependencies}")
        external = {dep for dep in links - internal if not dep.startswith("LLVM")}
        if external != set(spec.get("external", [])):
            failures.append(f"{name}: unregistered external dependency: {external}")
        graph[name] = internal | {dep for dep in public if dep.startswith("chtholly")} | set(spec.get("build", []))
        for include in props["public_includes"].split(";"):
            if not include:
                continue
            path = Path(include).resolve()
            module = name.removeprefix("chtholly_")
            expected = root / "toolchain" / module / "include"
            if name == "chtholly_test_support":
                expected = root / "testing/support/include"
            if path != expected.resolve():
                failures.append(f"{name}: unexpected public include root {include}")
        module = name.removeprefix("chtholly_")
        if module in lock["modules"]:
            expected_root = root / lock["modules"][module].get("public_include_root", f"toolchain/{module}/include")
            roots = {Path(path).resolve() for path in props["public_includes"].split(";") if path}
            if roots != {expected_root.resolve()}:
                failures.append(f"{name}: missing or incorrect module public include root")
        for include in props["includes"].split(";"):
            if include and Path(include).resolve() in (root, root / "toolchain"):
                failures.append(f"{name}: overly broad include root")
            if include:
                path = Path(include).resolve()
                if path.is_relative_to(root / "toolchain"):
                    parts = path.relative_to(root / "toolchain").parts
                    own_module = name.removeprefix("chtholly_")
                    if len(parts) < 2 or (parts[1] != "include" and parts[0] != own_module):
                        failures.append(f"{name}: cross-module private include directory {include}")
    cycles(graph)

    replies = build / ".cmake/api/v1/reply"
    indexes = sorted(replies.glob("index-*.json"))
    if not indexes:
        raise ValueError("missing CMake File API reply")
    index = json.loads(indexes[-1].read_text(encoding="utf-8"))
    model_object = next(obj for obj in index["objects"] if obj["kind"] == "codemodel")
    model = json.loads((replies / model_object["jsonFile"]).read_text(encoding="utf-8"))
    targets = model["configurations"][0]["targets"]
    by_id = {target["id"]: target["name"] for target in targets}
    compiled = {name for name in graph if exported[name].get("type") != "INTERFACE_LIBRARY"}
    if not compiled <= set(by_id.values()):
        failures.append(f"File API missing project targets: {compiled - set(by_id.values())}")
    def closure(name):
        result, pending = set(), list(graph.get(name, set()))
        while pending:
            dep = pending.pop()
            if dep not in result:
                result.add(dep)
                pending.extend(graph.get(dep, set()))
        return result
    for target in targets:
        name = target["name"]
        if name not in graph:
            continue
        data = json.loads((replies / target["jsonFile"]).read_text(encoding="utf-8"))
        actual = {by_id[item["id"]] for item in data.get("dependencies", [])}
        own = {dep for dep in actual if dep.startswith("chtholly")}
        direct = graph[name] & compiled
        if not direct <= own:
            failures.append(f"{name}: File API missing direct dependencies: {direct - own}")
        if not own <= closure(name):
            failures.append(f"{name}: File API dependency outside allowed closure: {own}")
        for group in data.get("compileGroups", []):
            for include in group.get("includes", []):
                path = Path(include["path"]).resolve()
                if path in (root, root / "toolchain"):
                    failures.append(f"{name}: File API found broad include root")
    cache_object = next(obj for obj in index["objects"] if obj["kind"] == "cache")
    cache = json.loads((replies / cache_object["jsonFile"]).read_text(encoding="utf-8"))
    values = {entry["name"]: entry["value"] for entry in cache["entries"]}
    frontend_only = values.get("CHTHOLLY_FRONTEND_ONLY") == "ON"
    required = {"chtholly_" + module for module, spec in lock["modules"].items()
                if not frontend_only or spec.get("platform") != "windows"}
    if not required <= exported.keys():
        failures.append(f"missing module targets: {required - exported.keys()}")
    testing = values.get("BUILD_TESTING") == "ON"
    required_targets = {name for name, spec in lock["targets"].items()
                        if (not spec.get("testing") or testing)
                        and (not frontend_only or spec.get("platform") != "windows")}
    if not required_targets <= exported.keys():
        failures.append(f"missing registered project targets: {required_targets - exported.keys()}")
    if frontend_only and ({"chtholly_codegen", "chtholly_driver", "chtholly_runtime", "chtholly_runtime_archive", "chthollyc"} & exported.keys()):
        failures.append("frontend configuration contains native product targets")
    if frontend_only:
        for name, props in exported.items():
            llvm_links = {dep for dep in dependencies(props["links"]) if dep.startswith("LLVM")}
            if llvm_links and (name != "chtholly_numeric" or llvm_links - {"LLVMSupport", "LLVMDemangle"}):
                failures.append(f"frontend target {name} depends on LLVM backend components")
    elif "runtime" in lock["modules"]:
        inspect_runtime(root, build)
    if values.get("BUILD_TESTING") == "OFF":
        if any("gtest" in name or name.endswith(("_tests", "_fixture")) or name.startswith("chtholly_test") for name in by_id.values()):
            failures.append("product-only configuration contains test targets")
        if any(spec.get("testing") and name in exported for name, spec in lock["targets"].items()):
            failures.append("product-only configuration exports test targets")
        if any(name.startswith("Python3_") and name.endswith("EXECUTABLE") for name in values):
            failures.append("product-only configuration discovered Python")
    return failures


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, required=True)
    args = parser.parse_args()
    try:
        failures = inspect(args.root.resolve(), args.build_dir.resolve())
        for failure in failures:
            print(failure, file=sys.stderr)
        return int(bool(failures))
    except (OSError, ValueError, KeyError, StopIteration, TypeError, IndexError) as error:
        print(f"architecture infrastructure error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
