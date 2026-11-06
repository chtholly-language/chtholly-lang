# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_sem_ir INTERFACE)
add_library(Chtholly::SemIR ALIAS chtholly_sem_ir)
target_compile_features(chtholly_sem_ir INTERFACE cxx_std_20)
target_include_directories(chtholly_sem_ir INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chtholly_sem_ir INTERFACE Chtholly::Numeric Chtholly::Source Chtholly::Types)
