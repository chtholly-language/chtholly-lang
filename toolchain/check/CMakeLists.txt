# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_check STATIC check.cpp)
add_library(Chtholly::Check ALIAS chtholly_check)
target_include_directories(chtholly_check PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chtholly_check PUBLIC Chtholly::SemIR
  PRIVATE Chtholly::Parse Chtholly::Diagnostic)
chtholly_configure_target(chtholly_check)
