# API tests — compile as C to verify C compatibility of bt2_api.h

# test_header: verifies the header compiles as C and types are accessible
add_executable(test_header test_header.c)
target_link_libraries(test_header bowtie2-align-s-lib)
set_target_properties(test_header PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)
add_test(NAME api-test-header
  COMMAND test_header
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# test_link: verifies library links and basic functions are callable
add_executable(test_link test_link.c)
target_link_libraries(test_link bowtie2-align-s-lib)
set_target_properties(test_link PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)
add_test(NAME api-test-link
  COMMAND test_link
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})

# test_config_init: verifies config defaults
add_executable(test_config_init test_config_init.c)
target_link_libraries(test_config_init bowtie2-align-s-lib)
set_target_properties(test_config_init PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON)
add_test(NAME api-test-config-init
  COMMAND test_config_init
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR})
