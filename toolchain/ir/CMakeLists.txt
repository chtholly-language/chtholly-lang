# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_ir STATIC verify.cpp)
add_library(Chtholly::IR ALIAS chtholly_ir)
target_include_directories(chtholly_ir PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chtholly_ir PUBLIC Chtholly::Numeric Chtholly::Source PRIVATE Chtholly::Diagnostic Chtholly::Target)
chtholly_configure_target(chtholly_ir)
