# SPDX-License-Identifier: MIT OR Apache-2.0
cmake_minimum_required(VERSION 3.25)
project(ChthollyTargetRuntime LANGUAGES C CXX)
if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR
   NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "18.1.8" OR
   NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Target runtime requires Windows x64 clang-cl 18.1.8")
endif()
set(builtins "${CHTHOLLY_LLVM_SOURCE_DIR}/compiler-rt/lib/builtins")
file(READ "${CHTHOLLY_ROOT}/cmake/dependencies.json" dependency_lock)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CHTHOLLY_ROOT}/cmake/dependencies.json")
string(JSON builtin_count LENGTH "${dependency_lock}" llvm_source builtins)
math(EXPR builtin_last "${builtin_count} - 1")
set(sources)
foreach(index RANGE 0 ${builtin_last})
  string(JSON name GET "${dependency_lock}" llvm_source builtins ${index})
  list(APPEND sources "${name}")
endforeach()
set(external_sources)
foreach(name IN LISTS sources)
  if(NOT EXISTS "${builtins}/${name}.c")
    message(FATAL_ERROR "Locked builtin source missing: ${name}.c")
  endif()
  list(APPEND external_sources "${builtins}/${name}.c")
endforeach()
add_library(chtholly_target_builtins OBJECT ${external_sources})
add_library(chtholly_target_runtime STATIC "${CHTHOLLY_ROOT}/toolchain/runtime/runtime.cpp"
  $<TARGET_OBJECTS:chtholly_target_builtins>)
foreach(target IN ITEMS chtholly_target_builtins chtholly_target_runtime)
  set_target_properties(${target} PROPERTIES MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
  target_compile_options(${target} PRIVATE /clang:-ffp-contract=off /clang:-fno-fast-math /fp:strict)
  target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _ITERATOR_DEBUG_LEVEL=0)
endforeach()
target_compile_features(chtholly_target_runtime PRIVATE cxx_std_20)
target_include_directories(chtholly_target_runtime PRIVATE
  "${CHTHOLLY_ROOT}/toolchain/runtime/include" "${CHTHOLLY_ROOT}/toolchain/target/include"
  "${CHTHOLLY_ROOT}/toolchain/types/include")
set_target_properties(chtholly_target_runtime PROPERTIES OUTPUT_NAME chtholly-runtime
  ARCHIVE_OUTPUT_DIRECTORY "${CHTHOLLY_RUNTIME_OUTPUT}")
file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/architecture/runtime.txt" CONTENT
"compiler=${CMAKE_CXX_COMPILER_ID}
version=${CMAKE_CXX_COMPILER_VERSION}
pointer_bytes=${CMAKE_SIZEOF_VOID_P}
crt=$<TARGET_PROPERTY:chtholly_target_runtime,MSVC_RUNTIME_LIBRARY>
includes=$<TARGET_PROPERTY:chtholly_target_runtime,INCLUDE_DIRECTORIES>
options=$<TARGET_PROPERTY:chtholly_target_runtime,COMPILE_OPTIONS>
")
add_custom_target(chtholly_runtime_metadata ALL
  COMMAND "${CMAKE_COMMAND}" -DARCHIVE=$<TARGET_FILE:chtholly_target_runtime>
    -DSOURCE_ROOT=${CHTHOLLY_LLVM_SOURCE_DIR} -DOUTPUT=${CHTHOLLY_RUNTIME_OUTPUT} -DCHTHOLLY_ROOT=${CHTHOLLY_ROOT}
    -P "${CMAKE_CURRENT_SOURCE_DIR}/metadata.cmake"
  DEPENDS chtholly_target_runtime
  BYPRODUCTS "${CHTHOLLY_RUNTIME_OUTPUT}/chtholly-runtime.meta"
    "${CHTHOLLY_RUNTIME_OUTPUT}/compiler-rt-LICENSE.txt"
  VERBATIM)
