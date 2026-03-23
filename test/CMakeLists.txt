add_executable(test_config test_config.c)
add_executable(test_compare_catalogs test_compare_catalogs.c)

if(SBSSLIB_BUILD_STATIC)
    target_link_libraries(test_config PRIVATE sbsslib_static)
elseif(SBSSLIB_BUILD_SHARED)
    target_link_libraries(test_config PRIVATE sbsslib_shared)
endif()

add_test(NAME sbsslib_test_config COMMAND test_config)

if(SBSSLIB_BUILD_CLI)
    set(SBSS_TEST_INPUT_DIR ${PROJECT_SOURCE_DIR}/test/input)
    set(SBSS_TEST_FITS ${SBSS_TEST_INPUT_DIR}/DESS_SURV_4_340255_260315_150024+051500_PR21433_01.fit)
    set(SBSS_TEST_SX_CONFIG ${SBSS_TEST_INPUT_DIR}/centu2.sex)
    set(SBSS_TEST_SX_CATALOG ${SBSS_TEST_INPUT_DIR}/DESS_SURV_4_340255_260315_150024+051500_PR21433_01.fit.sx)
    set(SBSS_TEST_SBSS_CATALOG ${SBSS_TEST_INPUT_DIR}/DESS_SURV_4_340255_260315_150024+051500_PR21433_01.fit.sbss)

    add_test(
        NAME sbsslib_generate_catalog_from_centu2
        COMMAND $<TARGET_FILE:sbssx> ${SBSS_TEST_FITS} -c ${SBSS_TEST_SX_CONFIG} -CATALOG_NAME ${SBSS_TEST_SBSS_CATALOG}
    )

    add_test(
        NAME sbsslib_compare_to_sextractor_flux_stage
        COMMAND test_compare_catalogs ${SBSS_TEST_SBSS_CATALOG} ${SBSS_TEST_SX_CATALOG}
    )

    set_tests_properties(sbsslib_compare_to_sextractor_flux_stage PROPERTIES DEPENDS sbsslib_generate_catalog_from_centu2)
endif()
