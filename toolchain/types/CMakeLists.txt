# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_types STATIC types.cpp)
add_library(Chtholly::Types ALIAS chtholly_types)
target_include_directories(chtholly_types PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
chtholly_configure_target(chtholly_types)
