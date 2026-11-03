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
                if include.startswith("llvm/") and ("include" in path.parts or module not in ("driver", "codegen")):
                    failures.append(f"{path}: LLVM outside private backend boundary")
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
            "public": ["chtholly_" + dep for dep in spec["public"]]}
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
        if any(dep.startswith("LLVM") for dep in links) and name not in ("chtholly_codegen", "chtholly_driver"):
            failures.append(f"{name}: unexpected LLVM dependency")
        external = {dep for dep in links - internal if not dep.startswith("LLVM")}
        if external != set(spec.get("external", [])):
            failures.append(f"{name}: unregistered external dependency: {external}")
        graph[name] = internal | {dep for dep in public if dep.startswith("chtholly")}
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
    if frontend_only and ({"chtholly_codegen", "chtholly_driver", "chthollyc"} & exported.keys()):
        failures.append("frontend configuration contains native product targets")
    if frontend_only and any(name.startswith("LLVM_") for name in values):
        failures.append("frontend configuration discovered LLVM")
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
    except (OSError, ValueError, KeyError, StopIteration) as error:
        print(f"architecture infrastructure error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
