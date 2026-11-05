# SPDX-License-Identifier: MIT OR Apache-2.0
include_guard(GLOBAL)
set(CHTHOLLY_LLVM_SOURCE_DIR "" CACHE PATH "Explicit clean locked llvm-project checkout")
if(NOT EXISTS "${CHTHOLLY_LLVM_SOURCE_DIR}/compiler-rt/lib/builtins/int_lib.h" OR
   NOT EXISTS "${CHTHOLLY_LLVM_SOURCE_DIR}/llvm/include/llvm/ADT/APFloat.h")
  message(FATAL_ERROR "Prepare locked LLVM sources and set CHTHOLLY_LLVM_SOURCE_DIR")
endif()
find_package(Git REQUIRED)
file(READ "${PROJECT_SOURCE_DIR}/cmake/dependencies.json" dependency_lock)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/cmake/dependencies.json")
string(JSON required_llvm_commit GET "${dependency_lock}" llvm_source commit)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CHTHOLLY_LLVM_SOURCE_DIR}" rev-parse HEAD
  OUTPUT_VARIABLE source_commit OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE identity_result)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CHTHOLLY_LLVM_SOURCE_DIR}" status --porcelain --untracked-files=all
  OUTPUT_VARIABLE source_dirty RESULT_VARIABLE dirty_result)
if(NOT identity_result EQUAL 0 OR NOT dirty_result EQUAL 0 OR NOT source_dirty STREQUAL "" OR
   NOT source_commit STREQUAL required_llvm_commit)
  message(FATAL_ERROR "LLVM sources must be a clean checkout of ${required_llvm_commit}")
endif()
message(STATUS "Locked LLVM sources: ${CHTHOLLY_LLVM_SOURCE_DIR} (${source_commit})")
