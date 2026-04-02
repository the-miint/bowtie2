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

# Python correctness comparison test
find_package(Python3 COMPONENTS Interpreter QUIET)
if (Python3_FOUND)
  add_test(NAME api-test-align-vs-native
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_align_vs_native.py
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  # Ensure native aligner and api_dump are built before the test runs
  set_tests_properties(api-test-align-vs-native PROPERTIES
    DEPENDS "api-test_align_basic")
endif()
