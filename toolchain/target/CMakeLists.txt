# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_target STATIC target.cpp)
add_library(Chtholly::Target ALIAS chtholly_target)
target_include_directories(chtholly_target PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
chtholly_configure_target(chtholly_target)
