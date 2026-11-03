# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_parse STATIC parse.cpp)
add_library(Chtholly::Parse ALIAS chtholly_parse)
target_include_directories(chtholly_parse PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chtholly_parse PUBLIC Chtholly::Lex PRIVATE Chtholly::Diagnostic)
chtholly_configure_target(chtholly_parse)
