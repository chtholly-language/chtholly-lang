# SPDX-License-Identifier: MIT OR Apache-2.0
function(chtholly_configure_target target)
  target_compile_features(${target} PUBLIC cxx_std_20)
  set_target_properties(${target} PROPERTIES
    CXX_EXTENSIONS OFF
    MSVC_RUNTIME_LIBRARY MultiThreadedDLL
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
  # Public STL types must use the same ABI as the pinned LLVM development kit.
  target_compile_definitions(${target} PUBLIC _ITERATOR_DEBUG_LEVEL=0)
  target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc /utf-8)
endfunction()

function(chtholly_use_llvm target)
  target_include_directories(${target} SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
  target_compile_options(${target} PRIVATE ${CHTHOLLY_LLVM_DEFINITIONS})
endfunction()
