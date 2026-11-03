# SPDX-License-Identifier: MIT OR Apache-2.0
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
if(NOT Python3_VERSION VERSION_LESS "3.12")
  message(FATAL_ERROR "Testing requires Python 3.11.x")
endif()
find_package(Git REQUIRED)
set(CHTHOLLY_GTEST_SOURCE_DIR "" CACHE PATH "Explicit checkout of locked Google Test")
file(READ "${PROJECT_SOURCE_DIR}/cmake/dependencies.json" dependency_lock)
string(JSON gtest_commit GET "${dependency_lock}" googletest commit)
if(NOT EXISTS "${CHTHOLLY_GTEST_SOURCE_DIR}/googletest/CMakeLists.txt")
  message(FATAL_ERROR "Set CHTHOLLY_GTEST_SOURCE_DIR to the locked Google Test checkout")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CHTHOLLY_GTEST_SOURCE_DIR}" rev-parse HEAD
  OUTPUT_VARIABLE gtest_actual OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE git_result)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${CHTHOLLY_GTEST_SOURCE_DIR}" status --porcelain --untracked-files=all
  OUTPUT_VARIABLE gtest_dirty RESULT_VARIABLE dirty_result)
if(NOT "${git_result}" STREQUAL "0" OR NOT "${dirty_result}" STREQUAL "0" OR
   NOT gtest_actual STREQUAL gtest_commit OR NOT gtest_dirty STREQUAL "")
  message(FATAL_ERROR "Google Test must be an unmodified checkout of ${gtest_commit}")
endif()
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_build_tests OFF CACHE BOOL "" FORCE)
set(gtest_build_samples OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
add_subdirectory("${CHTHOLLY_GTEST_SOURCE_DIR}" "${PROJECT_BINARY_DIR}/dependencies/googletest" EXCLUDE_FROM_ALL)
foreach(target IN ITEMS gtest gtest_main)
  chtholly_configure_target(${target})
endforeach()
include(GoogleTest)

function(chtholly_expect name labels)
  set_property(GLOBAL APPEND PROPERTY CHTHOLLY_EXPECTED_TESTS "${name}|${labels}")
endfunction()

function(chtholly_gtest target category capability)
  cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES;CASES" ${ARGN})
  add_executable(${target} ${ARG_SOURCES})
  target_link_libraries(${target} PRIVATE GTest::gtest_main chtholly_test_support ${ARG_LIBRARIES})
  chtholly_configure_target(${target})
  file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/testing/work/${target}")
  gtest_discover_tests(${target} DISCOVERY_MODE PRE_TEST DISCOVERY_TIMEOUT 30
    TEST_PREFIX "${category}." WORKING_DIRECTORY "${PROJECT_BINARY_DIR}/testing/work/${target}"
    TEST_LIST "${target}_discovered_tests" PROPERTIES TIMEOUT 60)
  # GoogleTest's generated property arguments flatten list-valued properties.
  # Apply LABELS directly in CTest, after PRE_TEST has registered the tests.
  set(labels_file "${CMAKE_CURRENT_BINARY_DIR}/${target}_labels.cmake")
  configure_file("${PROJECT_SOURCE_DIR}/cmake/GTestLabels.cmake.in" "${labels_file}" @ONLY)
  set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${labels_file}")
  foreach(case IN LISTS ARG_CASES)
    chtholly_expect("${category}.${case}" "${category},${capability}")
  endforeach()
endfunction()

function(chtholly_python_test name capability script)
  add_test(NAME "${name}" COMMAND "${Python3_EXECUTABLE}" "${script}" ${ARGN})
  string(REGEX REPLACE "\\..*" "" category "${name}")
  set_tests_properties("${name}" PROPERTIES TIMEOUT 180 LABELS "${category};${capability}"
    ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
  chtholly_expect("${name}" "${category},${capability}")
endfunction()

function(chtholly_case manifest)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${manifest}" data)
  string(JSON name GET "${data}" name)
  chtholly_python_test("${name}" native "${PROJECT_SOURCE_DIR}/testing/support/run_case.py"
    --compiler "$<TARGET_FILE:chthollyc>" --manifest "${CMAKE_CURRENT_SOURCE_DIR}/${manifest}"
    --work-dir "${PROJECT_BINARY_DIR}/testing/cases" --report-dir "${PROJECT_BINARY_DIR}/testing/reports")
  set_tests_properties("${name}" PROPERTIES TIMEOUT 60)
endfunction()
