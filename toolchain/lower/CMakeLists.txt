# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_lower STATIC lower.cpp)
add_library(Chtholly::Lower ALIAS chtholly_lower)
target_include_directories(chtholly_lower PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chtholly_lower PUBLIC Chtholly::IR Chtholly::SemIR PRIVATE Chtholly::Diagnostic Chtholly::Numeric Chtholly::Target)
chtholly_configure_target(chtholly_lower)
