# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_numeric STATIC numeric.cpp)
add_library(Chtholly::Numeric ALIAS chtholly_numeric)
target_include_directories(chtholly_numeric PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chtholly_numeric PUBLIC Chtholly::Target PRIVATE LLVMSupport LLVMDemangle)
chtholly_configure_target(chtholly_numeric)
chtholly_use_llvm(chtholly_numeric)
