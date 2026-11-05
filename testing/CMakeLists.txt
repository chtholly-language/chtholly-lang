# SPDX-License-Identifier: MIT OR Apache-2.0
add_subdirectory(support)
set(frontend_libraries Chtholly::Source Chtholly::Diagnostic Chtholly::Lex Chtholly::Parse
  Chtholly::Check Chtholly::SemIR Chtholly::Numeric Chtholly::Target Chtholly::IR Chtholly::Lower)
chtholly_gtest(chtholly_source_tests unit frontend SOURCES unit/source.cpp
  LIBRARIES Chtholly::Source CASES source.empty_eof source.line_endings source.byte_columns source.read_failure)
chtholly_gtest(chtholly_diagnostic_tests unit frontend SOURCES unit/diagnostic.cpp
  LIBRARIES Chtholly::Diagnostic CASES diagnostic.source_and_tool_errors)
chtholly_gtest(chtholly_lex_tests unit frontend SOURCES unit/lex.cpp
  LIBRARIES Chtholly::Lex CASES lex.token_spans lex.eof_span lex.identifier_boundaries
  lex.bom_and_comments lex.invalid_utf8 lex.unsupported_characters lex.core_tokens_and_nested_comments)
chtholly_gtest(chtholly_parse_tests unit frontend SOURCES unit/parse.cpp
  LIBRARIES Chtholly::Parse CASES parse.minimum_tree parse.structural_failures parse.functions_precedence_and_never)
chtholly_gtest(chtholly_check_tests unit frontend SOURCES unit/check.cpp
  LIBRARIES ${frontend_libraries} CASES
  check.integer_boundaries check.rejects_entry_type_and_overflow check.long_decimal
  check.binding_scope_and_fixed_types check.literals_conversions_and_recovery
  check.unreachable_checks_and_normal_entry check.labels_shadowing_and_returns
  check.divergence_preserves_static_checks check.rejects_postponed_features)
chtholly_gtest(chtholly_numeric_tests unit frontend SOURCES unit/numeric.cpp
  LIBRARIES Chtholly::Numeric CASES numeric.type_identity_and_integer_ranges
  numeric.arithmetic_and_bit_patterns numeric.conversion_order_and_finite_range
  numeric.ieee_literal_and_arithmetic_policies numeric.mathematical_values_and_resource_limits
  numeric.leading_zeroes_preserve_budget_and_value numeric.signed_zero_to_every_integer)
chtholly_gtest(chtholly_ir_tests unit frontend SOURCES unit/ir.cpp
  LIBRARIES ${frontend_libraries} CASES ir.accepts_typed_control_flow ir.rejects_corrupted_records
  ir.rejects_wrong_join_and_undefined_use)
chtholly_gtest(chtholly_pipeline_tests integration frontend SOURCES integration/compiler/pipeline.cpp
  LIBRARIES ${frontend_libraries} CASES frontend.checked_pipeline frontend.uninhabited_function_has_no_body
  frontend.canonical_termination_identity frontend.divergent_recovery_source)
if(NOT CHTHOLLY_FRONTEND_ONLY)
  chtholly_gtest(chtholly_codegen_tests integration native SOURCES integration/compiler/codegen.cpp
    LIBRARIES ${frontend_libraries} Chtholly::CodeGen CASES codegen.windows_coff codegen.canonical_abort_relocation
    codegen.block_storage_order_is_not_execution_order
    codegen.reordered_loop_parameters_and_dead_predecessor
    codegen.branch_edges_to_same_block_keep_arguments)
  add_executable(chtholly_object_fixture integration/compiler/object_fixture.cpp)
  target_link_libraries(chtholly_object_fixture PRIVATE chtholly_test_support ${frontend_libraries}
    Chtholly::CodeGen chtholly_codegen_test_adapter)
  target_include_directories(chtholly_object_fixture PRIVATE "${PROJECT_SOURCE_DIR}/testing/support/fixtures")
  chtholly_configure_target(chtholly_object_fixture)
  add_library(chtholly_native_bridge_fixture OBJECT support/fixtures/native_bridge.cpp)
  target_link_libraries(chtholly_native_bridge_fixture PRIVATE Chtholly::Runtime)
  target_compile_definitions(chtholly_native_bridge_fixture PRIVATE CHTHOLLY_BRIDGE_TEMPLATE)
  chtholly_configure_target(chtholly_native_bridge_fixture)
  set(native_cases f32_add f64_add subnormal negative_zero infinity nan float_range integer_float_range
    integer_float_precision float_nonfinite float_fractional float_integer integer_float bool_identity
    unit_argument unit_result root_replacement short_circuit argument_order recovery_side_effect recovery_lazy
    recovery_outer_name rhs_order mutual_recursion assert_once recovery_child_failure recovery_own_failure
    negative_float_range negative_shift_count nan_inequality nan_ordering zero_equality var_parameter)
  foreach(type IN ITEMS i8 i16 i32 i64 i128 u8 u16 u32 u64 u128 isize usize)
    foreach(operation IN ITEMS add overflow divide remainder shift multiply multiply_overflow subtract
        divide_zero shift_count right_shift bits negate negate_fail compare)
      list(APPEND native_cases "${type}_${operation}")
    endforeach()
    if(type MATCHES "^i")
      list(APPEND native_cases "${type}_divide_min")
    endif()
  endforeach()
  foreach(type IN ITEMS f32 f64)
    foreach(operation IN ITEMS subtract multiply divide negate overflow)
      list(APPEND native_cases "${type}_${operation}")
    endforeach()
  endforeach()
  foreach(source IN ITEMS i8 i16 i32 i64 i128 u8 u16 u32 u64 u128 isize usize f32 f64)
    foreach(destination IN ITEMS i8 i16 i32 i64 i128 u8 u16 u32 u64 u128 isize usize f32 f64)
      list(APPEND native_cases "convert_${source}_${destination}")
    endforeach()
  endforeach()
  foreach(case IN LISTS native_cases)
    chtholly_python_test("integration.numeric.${case}" native
      "${PROJECT_SOURCE_DIR}/testing/integration/compiler/native_numeric.py"
      --case "${case}" --fixture "$<TARGET_FILE:chtholly_object_fixture>"
      --clang "${CHTHOLLY_LLVM_BIN}/clang-cl.exe"
      --runtime "${PROJECT_BINARY_DIR}/bin/lib/chtholly-runtime.lib"
      --root "${PROJECT_SOURCE_DIR}" --work-dir "${PROJECT_BINARY_DIR}/testing/native")
  endforeach()
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
