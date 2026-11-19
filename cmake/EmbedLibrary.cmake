# SPDX-License-Identifier: GPL-3.0-or-later
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
set(library_generator "${CMAKE_CURRENT_SOURCE_DIR}/ci/library/embed.py")
set(library_input "${CMAKE_CURRENT_SOURCE_DIR}/data/library/library.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${library_input}" "${library_generator}" "${CMAKE_CURRENT_SOURCE_DIR}/ci/library/merge.py"
  "${CMAKE_CURRENT_SOURCE_DIR}/ci/library/audit.py"
  "${CMAKE_CURRENT_SOURCE_DIR}/data/names/standard-headers.txt")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${library_generator}"
  "${library_input}" "${CMAKE_CURRENT_BINARY_DIR}/embedded_library.cpp"
  RESULT_VARIABLE library_result OUTPUT_VARIABLE library_output ERROR_VARIABLE library_error)
if(NOT library_result EQUAL 0)
  message(FATAL_ERROR "Cannot embed library facts: ${library_error}")
endif()
message(STATUS "${library_output}")
