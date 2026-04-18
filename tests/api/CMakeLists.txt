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

# Phase 5 tests — error handling and log callback
add_api_test(test_error_no_exit)
add_api_test(test_error_recovery)
add_api_test(test_log_callback)

# Phase 6 tests — index builder API (link against build library)
macro(add_build_api_test name)
  add_executable(${name} ${name}.c)
  target_link_libraries(${name} bowtie2-build-s-lib)
  set_target_properties(${name} PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON)
  add_test(NAME api-${name}
    COMMAND ${name}
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
endmacro()

add_build_api_test(test_build_header)
add_build_api_test(test_build_config_init)
add_build_api_test(test_build_basic)
add_build_api_test(test_build_error)
add_build_api_test(test_build_context_reuse)
add_build_api_test(test_build_log_callback)
add_build_api_test(test_build_no_stdout)

# Phase 7 integration test — links combined lib (aligner + builder)
add_executable(test_build_then_align test_build_then_align.c)
target_link_libraries(test_build_then_align bowtie2-combined-s-lib)
set_target_properties(test_build_then_align PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)
add_test(NAME api-test_build_then_align
  COMMAND test_build_then_align
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# Reproducer for GPL-boundary macOS aarch64 SIGSEGV with all-N + no_unal
add_executable(test_repro_no_unal_all_n test_repro_no_unal_all_n.c)
target_link_libraries(test_repro_no_unal_all_n bowtie2-combined-s-lib)
set_target_properties(test_repro_no_unal_all_n PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)
add_test(NAME api-test_repro_no_unal_all_n
  COMMAND test_repro_no_unal_all_n
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# bench_api helper (used by performance benchmark)
add_executable(bench_api bench_api.c)
target_link_libraries(bench_api bowtie2-align-s-lib)
set_target_properties(bench_api PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)

# api_build helper (used by Python build comparison test)
add_executable(api_build api_build.c)
target_link_libraries(api_build bowtie2-build-s-lib)
set_target_properties(api_build PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)

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

  add_test(NAME api-test-build-vs-native
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_build_vs_native.py
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(api-test-build-vs-native PROPERTIES
    DEPENDS "api-test_build_basic")

  add_test(NAME api-test-options-vs-native
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_options_vs_native.py
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
  set_tests_properties(api-test-options-vs-native PROPERTIES
    DEPENDS "api-test_align_basic")
endif()
