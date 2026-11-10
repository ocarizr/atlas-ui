add_executable(atlas_asset_catalog_tests catalog_tests.cpp xml_tests.cpp)
target_link_libraries(atlas_asset_catalog_tests PRIVATE atlas::asset_catalog GTest::gtest_main)
atlas_configure_test(atlas_asset_catalog_tests)
gtest_discover_tests(atlas_asset_catalog_tests DISCOVERY_MODE PRE_TEST
    PROPERTIES LABELS asset-catalog TIMEOUT 20)
