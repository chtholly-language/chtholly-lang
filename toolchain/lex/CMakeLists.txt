# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_lex STATIC lex.cpp)
add_library(Chtholly::Lex ALIAS chtholly_lex)
target_include_directories(chtholly_lex PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chtholly_lex PUBLIC Chtholly::Source PRIVATE Chtholly::Diagnostic)
chtholly_configure_target(chtholly_lex)
