include_guard(GLOBAL)

option(ATLAS_ENABLE_LUA_DEBUGGER "Enable explicit local Lua development attachment" OFF)
option(ATLAS_SHIPPING_BUILD "Reject development-only capabilities" OFF)
if(ATLAS_SHIPPING_BUILD AND ATLAS_ENABLE_LUA_DEBUGGER)
    message(FATAL_ERROR "Shipping builds cannot enable the Lua debugger")
endif()
