# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(distill_inline_cases inline_cases.cpp)
target_link_libraries(distill_inline_cases PRIVATE distill_core)
target_compile_definitions(distill_inline_cases PRIVATE DISTILL_INLINE_CASES="${PROJECT_SOURCE_DIR}/tests/cases/inline")
add_test(NAME inline_cases COMMAND distill_inline_cases)
set_tests_properties(inline_cases PROPERTIES TIMEOUT 120)
