# SPDX-License-Identifier: MIT OR Apache-2.0
# Host-facing headers do not link target code into the compiler.
add_library(chtholly_runtime INTERFACE)
add_library(Chtholly::Runtime ALIAS chtholly_runtime)
target_include_directories(chtholly_runtime INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chtholly_runtime INTERFACE Chtholly::Target)
include(ExternalProject)
ExternalProject_Add(chtholly_runtime_archive
  BUILD_ALWAYS TRUE
  CMAKE_GENERATOR Ninja
  SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/build"
  BINARY_DIR "${PROJECT_BINARY_DIR}/target-runtime"
  DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
  CMAKE_ARGS -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=${CHTHOLLY_LLVM_BIN}/clang-cl.exe
    -DCMAKE_TOOLCHAIN_FILE=${PROJECT_SOURCE_DIR}/cmake/WindowsSDKManifest.cmake
    -DCMAKE_CXX_COMPILER=${CHTHOLLY_LLVM_BIN}/clang-cl.exe -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCHTHOLLY_ROOT=${PROJECT_SOURCE_DIR}
    -DCHTHOLLY_LLVM_SOURCE_DIR=${CHTHOLLY_LLVM_SOURCE_DIR}
    -DCHTHOLLY_RUNTIME_OUTPUT=${PROJECT_BINARY_DIR}/bin/lib
  INSTALL_COMMAND ""
  BUILD_BYPRODUCTS "${PROJECT_BINARY_DIR}/bin/lib/chtholly-runtime.lib"
    "${PROJECT_BINARY_DIR}/bin/lib/chtholly-runtime.meta")
ExternalProject_Add_Step(chtholly_runtime_archive file_api_query
  COMMAND "${CMAKE_COMMAND}" -DCHTHOLLY_FILE_API_DIRECTORY=${PROJECT_BINARY_DIR}/target-runtime
    -P "${PROJECT_SOURCE_DIR}/cmake/PrepareFileAPI.cmake"
  DEPENDEES mkdir DEPENDERS configure)
