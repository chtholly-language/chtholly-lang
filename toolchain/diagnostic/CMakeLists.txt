# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_diagnostic STATIC diagnostic.cpp)
add_library(Chtholly::Diagnostic ALIAS chtholly_diagnostic)
target_include_directories(chtholly_diagnostic PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chtholly_diagnostic PRIVATE Chtholly::Source)
chtholly_configure_target(chtholly_diagnostic)
