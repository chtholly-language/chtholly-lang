# SPDX-License-Identifier: MIT OR Apache-2.0
add_library(chtholly_test_support INTERFACE)
target_include_directories(chtholly_test_support INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(chtholly_test_support INTERFACE Chtholly::Source Chtholly::Diagnostic)
add_executable(chtholly_process_fixture fixtures/process.cpp)
find_package(Threads REQUIRED)
target_link_libraries(chtholly_process_fixture PRIVATE Threads::Threads)
chtholly_configure_target(chtholly_process_fixture)
chtholly_python_test(infrastructure.runner.contracts frontend
  "${CMAKE_CURRENT_SOURCE_DIR}/driver_tests.py" --fixture "$<TARGET_FILE:chtholly_process_fixture>")
chtholly_python_test(infrastructure.architecture.contracts frontend
  "${CMAKE_CURRENT_SOURCE_DIR}/architecture_tests.py" --root "${PROJECT_SOURCE_DIR}")
chtholly_python_test(infrastructure.checkers.contracts frontend
  "${CMAKE_CURRENT_SOURCE_DIR}/checker_tests.py" --root "${PROJECT_SOURCE_DIR}")
if(NOT CHTHOLLY_FRONTEND_ONLY)
  add_executable(chtholly_tool_fixture fixtures/tool.cpp)
  chtholly_configure_target(chtholly_tool_fixture)
endif()
