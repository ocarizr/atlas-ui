find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME lua_debug_host_protocol COMMAND "${Python3_EXECUTABLE}"
    "${PROJECT_SOURCE_DIR}/scripts/dev/debug_smoke.py" "$<TARGET_FILE:atlas_lua_debug_host>"
    "${PROJECT_SOURCE_DIR}/.tools/lua_dev/luapanda/LuaPanda.lua"
    "${PROJECT_SOURCE_DIR}/tests/scripting/data/debug_fixture.lua")
set_tests_properties(lua_debug_host_protocol PROPERTIES LABELS scripting TIMEOUT 30)
foreach(domain IN ITEMS ui application)
    add_test(NAME lua_debug_host_${domain}_domain COMMAND "${Python3_EXECUTABLE}"
        "${PROJECT_SOURCE_DIR}/scripts/dev/debug_smoke.py" "$<TARGET_FILE:atlas_lua_debug_host>"
        "${PROJECT_SOURCE_DIR}/.tools/lua_dev/luapanda/LuaPanda.lua"
        "${PROJECT_SOURCE_DIR}/tests/scripting/data/debug_fixture.lua" --domain ${domain})
    set_tests_properties(lua_debug_host_${domain}_domain PROPERTIES LABELS scripting TIMEOUT 30)
endforeach()
