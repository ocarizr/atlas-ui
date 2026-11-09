add_executable(atlas_lua_behavior_tests main.cpp fixture.cpp fixture_api.cpp)
target_link_libraries(atlas_lua_behavior_tests PRIVATE atlas::scripting unofficial::lua::lua-cpp)
atlas_configure_target(atlas_lua_behavior_tests)

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/reports")
add_test(NAME lua_behavior_suites COMMAND atlas_lua_behavior_tests
    "${PROJECT_SOURCE_DIR}/.tools/lua_dev/luaunit/luaunit.lua"
    "${CMAKE_CURRENT_SOURCE_DIR}/runner.lua"
    "${CMAKE_CURRENT_SOURCE_DIR}/data"
    "${PROJECT_BINARY_DIR}/reports/lua-behavior-$<CONFIG>.xml"
    "${CMAKE_CURRENT_SOURCE_DIR}/domain_suite.lua"
    "${CMAKE_CURRENT_SOURCE_DIR}/failure_suite.lua")
set_tests_properties(lua_behavior_suites PROPERTIES LABELS scripting TIMEOUT 30)
