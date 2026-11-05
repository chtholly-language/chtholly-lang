# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_codegen STATIC codegen.cpp emitter.cpp)
add_library(Chtholly::CodeGen ALIAS chtholly_codegen)
target_include_directories(chtholly_codegen PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${PROJECT_BINARY_DIR}/generated")
llvm_map_components_to_libnames(chtholly_codegen_llvm
  Core Support CodeGen Target MC X86CodeGen X86Desc X86Info)
target_link_libraries(chtholly_codegen PRIVATE Chtholly::IR Chtholly::Numeric
  Chtholly::Target Chtholly::Runtime Chtholly::Diagnostic ${chtholly_codegen_llvm})
chtholly_configure_target(chtholly_codegen)
chtholly_use_llvm(chtholly_codegen)
if(BUILD_TESTING)
  add_library(chtholly_codegen_test_adapter STATIC test_adapter.cpp)
  target_link_libraries(chtholly_codegen_test_adapter PUBLIC Chtholly::CodeGen)
  chtholly_configure_target(chtholly_codegen_test_adapter)
endif()
