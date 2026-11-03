# SPDX-License-Identifier: MIT OR Apache-2.0
find_package(LLVM 18.1.8 EXACT CONFIG REQUIRED)
if(NOT "X86" IN_LIST LLVM_TARGETS_TO_BUILD)
  message(FATAL_ERROR "LLVM ${LLVM_PACKAGE_VERSION} must include the X86 target")
endif()

set(CHTHOLLY_LLVM_BIN "${LLVM_TOOLS_BINARY_DIR}")
foreach(tool IN ITEMS clang-cl lld-link)
  set(tool_path "${CHTHOLLY_LLVM_BIN}/${tool}.exe")
  if(NOT EXISTS "${tool_path}")
    message(FATAL_ERROR "Selected LLVM installation is missing ${tool_path}")
  endif()
  execute_process(COMMAND "${tool_path}" --version
    RESULT_VARIABLE tool_status OUTPUT_VARIABLE tool_stdout ERROR_VARIABLE tool_stderr
    TIMEOUT 15)
  if(NOT "${tool_status}" STREQUAL "0")
    message(FATAL_ERROR "Cannot query ${tool_path}: ${tool_status}\n${tool_stderr}")
  endif()
  if(NOT "${tool_stdout}${tool_stderr}" MATCHES "(^|[^0-9])18\\.1\\.8([^0-9.]|$)")
    message(FATAL_ERROR "${tool_path} must match LLVM 18.1.8:\n${tool_stdout}${tool_stderr}")
  endif()
  string(STRIP "${tool_stdout}${tool_stderr}" tool_version)
  message(STATUS "${tool_path}: ${tool_version}")
endforeach()

message(STATUS "CMake: ${CMAKE_COMMAND} (${CMAKE_VERSION})")
message(STATUS "MSVC: ${CMAKE_CXX_COMPILER} (${CMAKE_CXX_COMPILER_VERSION})")
message(STATUS "LLVM: ${LLVM_DIR} (${LLVM_PACKAGE_VERSION})")
message(STATUS "LLVM targets: ${LLVM_TARGETS_TO_BUILD}")
message(STATUS "LLVM assertions / RTTI / EH: ${LLVM_ENABLE_ASSERTIONS} / ${LLVM_ENABLE_RTTI} / ${LLVM_ENABLE_EH}")
separate_arguments(CHTHOLLY_LLVM_DEFINITIONS NATIVE_COMMAND "${LLVM_DEFINITIONS}")

# CMake-normalized paths use forward slashes; escape before embedding in C++.
file(TO_CMAKE_PATH "${CHTHOLLY_LLVM_BIN}" CHTHOLLY_CONFIGURED_LLVM_BIN)
string(REPLACE "\\" "\\\\" CHTHOLLY_CONFIGURED_LLVM_BIN "${CHTHOLLY_CONFIGURED_LLVM_BIN}")
string(REPLACE "\"" "\\\"" CHTHOLLY_CONFIGURED_LLVM_BIN "${CHTHOLLY_CONFIGURED_LLVM_BIN}")
configure_file("${PROJECT_SOURCE_DIR}/cmake/build_config.h.in"
  "${PROJECT_BINARY_DIR}/generated/Chtholly/build_config.h" @ONLY)
