add_executable(test_config test_config.c)

if(SBSSLIB_BUILD_STATIC)
    target_link_libraries(test_config PRIVATE sbsslib_static)
elseif(SBSSLIB_BUILD_SHARED)
    target_link_libraries(test_config PRIVATE sbsslib_shared)
endif()

add_test(NAME sbsslib_test_config COMMAND test_config)
