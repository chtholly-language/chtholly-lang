# SPDX-License-Identifier: MIT OR Apache-2.0
import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

CHECKER = None


class ArchitectureTests(unittest.TestCase):
    def test_runtime_subbuild_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            build = root / "out"
            runtime = build / "target-runtime"
            reply = runtime / ".cmake/api/v1/reply"
            reply.mkdir(parents=True)
            (root / "cmake").mkdir()
            (root / "cmake/dependencies.json").write_text(json.dumps({"llvm_source": {"builtins": ["divti3"]}}), encoding="utf-8")
            external = root / "external"
            objects = [{"kind": "codemodel", "jsonFile": "model.json"}, {"kind": "cache", "jsonFile": "cache.json"}]
            (reply / "index-1.json").write_text(json.dumps({"objects": objects}), encoding="utf-8")
            targets = [{"id": "runtime", "name": "chtholly_target_runtime", "jsonFile": "runtime.json"},
                       {"id": "builtins", "name": "chtholly_target_builtins", "jsonFile": "builtins.json"},
                       {"id": "metadata", "name": "chtholly_runtime_metadata", "jsonFile": "metadata.json"}]
            (reply / "model.json").write_text(json.dumps({"configurations": [{"targets": targets}]}), encoding="utf-8")
            entries = [
                {"name": "CHTHOLLY_LLVM_SOURCE_DIR", "value": str(external)},
                {"name": "CMAKE_TOOLCHAIN_FILE", "value": str(root / "cmake/WindowsSDKManifest.cmake")},
                {"name": "CMAKE_MT", "value": str(root / "sdk/bin/10.0.22621.0/x64/mt.exe")},
            ]
            cache_file = reply / "cache.json"
            cache_file.write_text(json.dumps({"entries": entries}), encoding="utf-8")
            source = root / "toolchain/runtime/runtime.cpp"
            runtime_target = {"sources": [{"path": str(source), "compileGroupIndex": 0}], "dependencies": [{"id": "builtins"}]}
            (reply / "runtime.json").write_text(json.dumps(runtime_target), encoding="utf-8")
            (reply / "metadata.json").write_text(json.dumps({"dependencies": [{"id": "runtime"}]}), encoding="utf-8")
            (reply / "builtins.json").write_text(json.dumps({"sources": [{
                "path": str(external / "compiler-rt/lib/builtins/divti3.c"), "compileGroupIndex": 0}]}), encoding="utf-8")
            properties = runtime / "architecture/runtime.txt"
            properties.parent.mkdir()
            record = ("compiler=Clang\nversion=18.1.8\npointer_bytes=8\ncrt=MultiThreadedDLL\n"
                      f"includes={root / 'toolchain/runtime/include'};{root / 'toolchain/target/include'};{root / 'toolchain/types/include'}\noptions=/fp:strict\n")
            properties.write_text(record, encoding="utf-8")
            CHECKER.inspect_runtime(root, build)
            for wrong_tool in ("", str(root / "llvm/bin/llvm-mt.exe"), str(root / "sdk/bin/x86/mt.exe")):
                entries[2]["value"] = wrong_tool
                cache_file.write_text(json.dumps({"entries": entries}), encoding="utf-8")
                with self.assertRaisesRegex(ValueError, "Windows SDK x64 mt.exe"):
                    CHECKER.inspect_runtime(root, build)
            entries[2]["value"] = str(root / "sdk/bin/10.0.22621.0/x64/mt.exe")
            entries[1]["value"] = str(root / "cmake/other.cmake")
            cache_file.write_text(json.dumps({"entries": entries}), encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "SDK manifest toolchain"):
                CHECKER.inspect_runtime(root, build)
            entries[1]["value"] = str(root / "cmake/WindowsSDKManifest.cmake")
            cache_file.write_text(json.dumps({"entries": entries}), encoding="utf-8")
            properties.write_text(record.replace("MultiThreadedDLL", "MultiThreadedDebugDLL"), encoding="utf-8")
            with self.assertRaises(ValueError):
                CHECKER.inspect_runtime(root, build)
            properties.write_text(record, encoding="utf-8")
            runtime_target["compileGroups"] = [{"compileCommandFragments": [{"fragment": "-fsanitize=address"}]}]
            (reply / "runtime.json").write_text(json.dumps(runtime_target), encoding="utf-8")
            with self.assertRaises(ValueError):
                CHECKER.inspect_runtime(root, build)

    def test_cycles(self):
        CHECKER.cycles({"a": {"b"}, "b": set()})
        with self.assertRaises(ValueError):
            CHECKER.cycles({"a": {"b"}, "b": {"a"}})

    def test_static_private_propagation(self):
        self.assertEqual(CHECKER.public_dependencies("$<LINK_ONLY:chtholly_source>;Chtholly::Lex"), {"chtholly_lex"})

    def test_private_and_llvm_include_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "toolchain/source/include/Chtholly/Source/source.h"
            header.parent.mkdir(parents=True)
            modules = {"source": {"links": []}}
            header.write_text("#include <string>\n", encoding="utf-8")
            self.assertEqual(CHECKER.scan_includes(root, modules), [])
            header.write_text('#include "../private.h"\n#include <llvm/IR/Module.h>\n', encoding="utf-8")
            self.assertEqual(len(CHECKER.scan_includes(root, modules)), 2)

    def test_public_propagation_and_broad_roots(self):
        self.assertEqual(CHECKER.public_dependencies("$<LINK_ONLY:LLVMCore>"), set())
        self.assertEqual(CHECKER.dependencies("Chtholly::Source;Chtholly::SemIR"),
                         {"chtholly_source", "chtholly_sem_ir"})

    def test_public_headers_cannot_use_private_dependencies(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            header = root / "toolchain/lex/include/Chtholly/Lex/lex.h"
            dependency = root / "toolchain/diagnostic/include/Chtholly/Diagnostic/diagnostic.h"
            for path in (header, dependency):
                path.parent.mkdir(parents=True)
                path.write_text("", encoding="utf-8")
            modules = {"lex": {"links": ["diagnostic"], "public": []},
                       "diagnostic": {"links": [], "public": []}}
            header.write_text("#include <Chtholly/Diagnostic/diagnostic.h>\n", encoding="utf-8")
            self.assertTrue(CHECKER.scan_includes(root, modules))
            modules["lex"]["public"] = ["diagnostic"]
            self.assertEqual(CHECKER.scan_includes(root, modules), [])

    def test_numeric_llvm_permission_is_private_and_component_limited(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            private = root / "toolchain/numeric/numeric.cpp"
            private.parent.mkdir(parents=True)
            modules = {"numeric": {"links": [], "public": [], "llvm_headers": ["llvm/ADT/", "llvm/Support/"]}}
            private.write_text("#include <llvm/ADT/APInt.h>\n", encoding="utf-8")
            self.assertEqual(CHECKER.scan_includes(root, modules), [])
            private.write_text("#include <llvm/IR/Module.h>\n", encoding="utf-8")
            self.assertTrue(CHECKER.scan_includes(root, modules))
            header = root / "toolchain/numeric/include/value.h"
            header.parent.mkdir()
            header.write_text("#include <llvm/ADT/APInt.h>\n", encoding="utf-8")
            self.assertEqual(len(CHECKER.scan_includes(root, modules)), 2)

    def test_file_api_and_property_fixtures(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "cmake").mkdir()
            (root / "cmake/module-dependencies.json").write_text(json.dumps({
                "modules": {"source": {"links": [], "public": [], "platform": "frontend"}}, "targets": {}}), encoding="utf-8")
            build = root / "out"
            exports = build / "architecture"
            exports.mkdir(parents=True)
            props = exports / "chtholly_source.txt"
            public_root = (root / "toolchain/source/include").as_posix()
            properties = f"name=chtholly_source\nlinks=\ninterface_links=\npublic_includes={public_root}\nincludes=\n"
            props.write_text(properties, encoding="utf-8")
            reply = build / ".cmake/api/v1/reply"
            reply.mkdir(parents=True)
            (reply / "index-1.json").write_text(json.dumps({"objects": [
                {"kind": "codemodel", "jsonFile": "model.json"}, {"kind": "cache", "jsonFile": "cache.json"}]}), encoding="utf-8")
            (reply / "model.json").write_text(json.dumps({"configurations": [{"targets": [
                {"id": "source-id", "name": "chtholly_source", "jsonFile": "target.json"}]}]}), encoding="utf-8")
            (reply / "target.json").write_text('{"dependencies":[]}', encoding="utf-8")
            (reply / "cache.json").write_text('{"entries":[]}', encoding="utf-8")
            self.assertEqual(CHECKER.inspect(root, build), [])
            props.write_text(properties + f"includes={root.as_posix()}\n", encoding="utf-8")
            self.assertTrue(CHECKER.inspect(root, build))
            props.write_text(properties.replace("links=\n", "links=Chtholly::Lex\n", 1), encoding="utf-8")
            self.assertTrue(CHECKER.inspect(root, build))
            props.write_text(properties, encoding="utf-8")
            (reply / "cache.json").write_text(json.dumps({"entries": [
                {"name": "BUILD_TESTING", "value": "OFF"}]}), encoding="utf-8")
            model = {"configurations": [{"targets": [
                {"id": "source-id", "name": "chtholly_source", "jsonFile": "target.json"},
                {"id": "gtest-id", "name": "gtest", "jsonFile": "unused.json"}]}]}
            (reply / "model.json").write_text(json.dumps(model), encoding="utf-8")
            self.assertIn("product-only configuration contains test targets", CHECKER.inspect(root, build))
            (reply / "model.json").write_text('{"configurations":[{"targets":[]}]}', encoding="utf-8")
            self.assertTrue(CHECKER.inspect(root, build))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    args, remaining = parser.parse_known_args()
    spec = importlib.util.spec_from_file_location("architecture", args.root / "scripts/check-architecture.py")
    CHECKER = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(CHECKER)
    unittest.main(argv=[__file__, *remaining])
