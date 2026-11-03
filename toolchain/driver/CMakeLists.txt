# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_driver STATIC driver.cpp)
add_library(Chtholly::Driver ALIAS chtholly_driver)
target_include_directories(chtholly_driver PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${PROJECT_BINARY_DIR}/generated")
llvm_map_components_to_libnames(chtholly_driver_llvm Support)
target_link_libraries(chtholly_driver PRIVATE Chtholly::Source Chtholly::Diagnostic
  Chtholly::Lex Chtholly::Parse Chtholly::SemIR Chtholly::Check Chtholly::CodeGen
  ${chtholly_driver_llvm})
chtholly_configure_target(chtholly_driver)
chtholly_use_llvm(chtholly_driver)

add_executable(chthollyc main.cpp)
target_link_libraries(chthollyc PRIVATE Chtholly::Driver)
chtholly_configure_target(chthollyc)
