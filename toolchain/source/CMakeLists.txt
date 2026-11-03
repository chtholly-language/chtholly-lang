# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_source STATIC source.cpp)
add_library(Chtholly::Source ALIAS chtholly_source)
target_include_directories(chtholly_source PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
chtholly_configure_target(chtholly_source)
