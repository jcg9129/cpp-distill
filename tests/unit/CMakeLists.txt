# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(distill_test_helper helper.cpp)
target_link_libraries(distill_test_helper PRIVATE distill_core)
if(WIN32)
  target_compile_definitions(distill_test_helper PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
add_executable(distill_unit_tests main.cpp command_test.cpp process_test.cpp probe_test.cpp lex_test.cpp names_test.cpp inline_test.cpp profile_test.cpp judge_probe_test.cpp resolve_test.cpp validate_test.cpp option_model_test.cpp prune_test.cpp render_test.cpp shorten_names_test.cpp pipeline_test.cpp)
target_link_libraries(distill_unit_tests PRIVATE distill_core distill_test_evaluate)
target_compile_definitions(distill_unit_tests PRIVATE
  DISTILL_EXPECTED_VERSION="${DISTILL_VERSION}"
  DISTILL_EXPECTED_COMMIT="${DISTILL_COMMIT}"
  DISTILL_EXPECTED_LLVM_REVISION="${DISTILL_LLVM_REVISION}")
target_compile_definitions(distill_unit_tests PRIVATE DISTILL_EXTENSION_DATA="${PROJECT_SOURCE_DIR}/data/extensions" DISTILL_PROFILE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/profile" DISTILL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/probe")
add_dependencies(distill_unit_tests distill_test_helper)
add_test(NAME unit COMMAND distill_unit_tests)
# Full probe/pipeline units and readable shortening regressions on bounded hosts.
set_tests_properties(unit PROPERTIES TIMEOUT 240)
# Replace the baseline object in these test binaries with a synthetic baseline
# emitted by the real merger/embedder. The committed matrix data stays untouched.
set(member_fixture "${CMAKE_CURRENT_BINARY_DIR}/embedded_members.cpp")
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${PROJECT_SOURCE_DIR}/ci/library/fixtures/embedded_members.py" "${member_fixture}"
  RESULT_VARIABLE member_fixture_result)
if(NOT member_fixture_result EQUAL 0)
  message(FATAL_ERROR "Cannot generate embedded member inventory test fixture")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/ci/library/fixtures/embedded_members.py")
add_executable(distill_embedded_members_test embedded_members_test.cpp "${member_fixture}")
target_link_libraries(distill_embedded_members_test PRIVATE distill_core)
target_compile_definitions(distill_embedded_members_test PRIVATE DISTILL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/probe")
if(TARGET distill_clang)
  add_executable(distill_inventory_test_worker "${PROJECT_SOURCE_DIR}/src/clang/prune_worker.cpp" "${member_fixture}")
  target_link_libraries(distill_inventory_test_worker PRIVATE distill_clang)
  add_test(NAME embedded_members COMMAND distill_embedded_members_test "$<TARGET_FILE:distill_inventory_test_worker>")
else()
  add_test(NAME embedded_members COMMAND distill_embedded_members_test)
endif()
set_tests_properties(embedded_members PROPERTIES TIMEOUT 60)
set(macro_fixture "${CMAKE_CURRENT_BINARY_DIR}/embedded_standard_macros.cpp")
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${PROJECT_SOURCE_DIR}/ci/library/fixtures/embedded_standard_macros.py" "${macro_fixture}"
  RESULT_VARIABLE macro_fixture_result)
if(NOT macro_fixture_result EQUAL 0)
  message(FATAL_ERROR "Cannot generate standard library macro test fixture")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/ci/library/fixtures/embedded_standard_macros.py")
add_executable(distill_library_macros_test library_macros_test.cpp "${macro_fixture}")
target_link_libraries(distill_library_macros_test PRIVATE distill_core distill_test_evaluate)
target_compile_definitions(distill_library_macros_test PRIVATE DISTILL_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/probe")
add_test(NAME library_macros COMMAND distill_library_macros_test)
set_tests_properties(library_macros PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77)
if(TARGET distill_clang)
  add_test(NAME library_macros_clang COMMAND distill_library_macros_test)
  add_test(NAME library_macros_differential COMMAND distill_library_macros_test)
  set_tests_properties(library_macros_clang PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 ENVIRONMENT "DISTILL_TEST_EVALUATOR=clang")
  set_tests_properties(library_macros_differential PROPERTIES TIMEOUT 60 SKIP_RETURN_CODE 77 ENVIRONMENT "DISTILL_TEST_EVALUATOR=differential")
  add_test(NAME resolver_clang COMMAND distill_unit_tests --filter resolve_)
  set_tests_properties(resolver_clang PROPERTIES TIMEOUT 120 ENVIRONMENT "DISTILL_TEST_EVALUATOR=clang")
  add_test(NAME resolver_differential COMMAND distill_unit_tests --filter resolve_)
  set_tests_properties(resolver_differential PROPERTIES TIMEOUT 120 ENVIRONMENT "DISTILL_TEST_EVALUATOR=differential")
endif()

if(TARGET distill)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  add_test(NAME judge_diagnostic_cli COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/ci/profiles/test_probe_cli.py" "$<TARGET_FILE:distill>"
    "${PROJECT_SOURCE_DIR}/tests/unit/fixtures/profile")
  set_tests_properties(judge_diagnostic_cli PROPERTIES TIMEOUT 60)
endif()
