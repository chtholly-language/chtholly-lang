# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_flow STATIC flow.cpp)
add_library(Chtholly::Flow ALIAS chtholly_flow)
target_include_directories(chtholly_flow PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chtholly_flow PUBLIC Chtholly::IR PRIVATE Chtholly::Diagnostic)
chtholly_configure_target(chtholly_flow)
