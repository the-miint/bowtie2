# API tests — compile as C to verify C compatibility of bt2_api.h

# Helper macro for C API test executables
macro(add_api_test name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} bowtie2-align-s-lib)
  set_target_properties(${name} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)
  add_test(NAME api-${name}
    COMMAND ${name}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endmacro()

# Phase 0 tests
add_api_test(test_header)
add_api_test(test_link)
add_api_test(test_config_init)

# Phase 1 tests
add_api_test(test_align_basic)
add_api_test(test_align_paired)
add_api_test(test_context_reuse)
add_api_test(test_error_invalid_index)
add_api_test(test_error_null)

# api_dump helper (used by Python comparison tests)
add_executable(api_dump api_dump.c)
target_link_libraries(api_dump bowtie2-align-s-lib)
set_target_properties(api_dump PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)

# Phase 3 tests — in-memory input
add_api_test(test_input_memory)
add_api_test(test_input_paired_memory)
add_api_test(test_input_empty)
add_api_test(test_input_no_qual)

# api_dump_memory helper (used by Python memory-vs-file comparison tests)
add_executable(api_dump_memory api_dump_memory.c)
target_link_libraries(api_dump_memory bowtie2-align-s-lib)
set_target_properties(api_dump_memory PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)

# Phase 4 tests — direct driver path (no temp files)
add_api_test(test_direct_no_tempfile)

# Python correctness comparison tests
find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_FOUND)
  add_test(NAME api-test-align-vs-native
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_align_vs_native.py
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(api-test-align-vs-native PROPERTIES
    DEPENDS "api-test_align_basic")

  add_test(NAME api-test-input-memory-vs-file
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_input_memory_vs_file.py
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(api-test-input-memory-vs-file PROPERTIES
    DEPENDS "api-test_input_memory")
endif()
