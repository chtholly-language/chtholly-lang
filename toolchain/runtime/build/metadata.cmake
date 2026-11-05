# SPDX-License-Identifier: MIT OR Apache-2.0
file(SHA256 "${ARCHIVE}" archive_hash)
set(runtime_identity "")
foreach(component IN ITEMS runtime/runtime.cpp runtime/include/Chtholly/Runtime/abi.h target/include/Chtholly/Target/target.h)
  file(SHA256 "${CHTHOLLY_ROOT}/toolchain/${component}" component_hash)
  string(APPEND runtime_identity "${component_hash}")
endforeach()
string(SHA256 implementation "${runtime_identity}")
file(WRITE "${OUTPUT}/chtholly-runtime.meta"
  "abi=2\ntarget=x86_64-pc-windows-msvc\nllvm=18.1.8\nsource=3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff\nsha256=${archive_hash}\nimplementation=${implementation}\n")
configure_file("${SOURCE_ROOT}/compiler-rt/LICENSE.TXT" "${OUTPUT}/compiler-rt-LICENSE.txt" COPYONLY)
