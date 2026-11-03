# SPDX-License-Identifier: MIT OR Apache-2.0
add_subdirectory(support)
set(frontend_libraries Chtholly::Source Chtholly::Diagnostic Chtholly::Lex Chtholly::Parse Chtholly::Check)
chtholly_gtest(chtholly_source_tests unit frontend SOURCES unit/source.cpp
  LIBRARIES Chtholly::Source CASES source.empty_eof source.line_endings source.byte_columns source.read_failure)
chtholly_gtest(chtholly_diagnostic_tests unit frontend SOURCES unit/diagnostic.cpp
  LIBRARIES Chtholly::Diagnostic CASES diagnostic.source_and_tool_errors)
chtholly_gtest(chtholly_lex_tests unit frontend SOURCES unit/lex.cpp
  LIBRARIES Chtholly::Lex CASES lex.token_spans lex.eof_span lex.identifier_boundaries
  lex.bom_and_comments lex.invalid_utf8 lex.unsupported_characters)
chtholly_gtest(chtholly_parse_tests unit frontend SOURCES unit/parse.cpp
  LIBRARIES Chtholly::Parse CASES parse.minimum_tree parse.structural_failures)
chtholly_gtest(chtholly_check_tests unit frontend SOURCES unit/check.cpp
  LIBRARIES Chtholly::Lex Chtholly::Parse Chtholly::Check CASES
  check.integer_boundaries check.rejects_entry_type_and_overflow check.long_decimal)
chtholly_gtest(chtholly_pipeline_tests integration frontend SOURCES integration/compiler/pipeline.cpp
  LIBRARIES ${frontend_libraries} CASES frontend.checked_pipeline)
if(NOT CHTHOLLY_FRONTEND_ONLY)
  chtholly_gtest(chtholly_codegen_tests integration native SOURCES integration/compiler/codegen.cpp
    LIBRARIES ${frontend_libraries} Chtholly::CodeGen CASES codegen.windows_coff)
  add_subdirectory(regression)
  add_subdirectory(e2e)
  chtholly_python_test(integration.toolchain.failures toolchain
    "${PROJECT_SOURCE_DIR}/testing/integration/toolchain/driver.py"
    --compiler "$<TARGET_FILE:chthollyc>" --fixture "$<TARGET_FILE:chtholly_tool_fixture>"
    --work-dir "${PROJECT_BINARY_DIR}/testing/toolchain")
endif()

get_property(expected GLOBAL PROPERTY CHTHOLLY_EXPECTED_TESTS)
file(WRITE "${PROJECT_BINARY_DIR}/testing/expected-tests.txt" "")
foreach(entry IN LISTS expected)
  file(APPEND "${PROJECT_BINARY_DIR}/testing/expected-tests.txt" "${entry}\n")
endforeach()
