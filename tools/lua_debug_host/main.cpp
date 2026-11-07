/** @file @brief Developer fixture host for debugging the actual embedded Lua C++ interpreter. */
#include "transport.hpp"
#include <atlas/scripting/session.hpp>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

namespace
{
#if ATLAS_NATIVE_LUA_DEBUG_HOST
/// @brief Read developer fixtures, never application assets or packaged project content.
std::string read_fixture(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary};
    if (!file)
    {
        throw std::runtime_error("Cannot read Lua developer fixture: " + path.generic_string());
    }

    std::string bytes(524289, '\0');
    file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    bytes.resize(static_cast<std::size_t>(file.gcount()));
    if (bytes.size() > 524288 || file.bad())
    {
        throw std::runtime_error("Lua developer fixture exceeds 512 KiB");
    }

    return bytes;
}
#endif
} // namespace

/// @brief Run one deterministic owner-thread tick; a debugger pause advances no simulation time.
static int run_host(int argc, char** argv)
{
    if (argc == 2 && std::string_view{argv[1]} == "--version")
    {
        std::cout << "Atlas Lua development host / embedded Lua 5.5.1 C++\n";
        return 0;
    }

    if (!atlas::scripting::session::debugger_available())
    {
        std::cerr
            << "Configure with ATLAS_ENABLE_LUA_DEBUGGER=ON; never enable in shipping builds.\n";
        return 1;
    }

#if ATLAS_NATIVE_LUA_DEBUG_HOST
    try
    {
        if (argc != 4)
        {
            throw std::invalid_argument(
                "Usage: atlas_lua_debug_host <script.lua> <LuaPanda.lua> <port>");
        }

        const auto port = std::stoul(argv[3]);
        if (!port || port > 65535)
        {
            throw std::invalid_argument("Debugger port must be 1..65535");
        }

        auto model =
            std::make_shared<atlas::mvvm::view_model>(atlas::mvvm::compile({"debug_fixture"}));
        atlas::scripting::session behavior{model};
        const auto source =
            "@" + std::filesystem::absolute(argv[1]).lexically_normal().generic_string();
        behavior.load(read_fixture(argv[1]), source);
        behavior.attach_debugger({read_fixture(argv[2]),
                                  atlas::scripting::development::make_loopback_transport(),
                                  static_cast<std::uint16_t>(port)});
        model->begin_frame(1);
        behavior.begin_frame(1);
        behavior.call("tick");
        behavior.detach_debugger();
        std::cout << "Completed owner-thread tick\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
#else
    std::cerr << "The Lua developer socket adapter requires Windows integration.\n";
    return 1;
#endif
}

/// @brief Keep unexpected tooling exceptions within the executable failure boundary.
int main(int argc, char** argv)
{
    try
    {
        return run_host(argc, argv);
    }
    catch (...)
    {
        std::fputs("Unexpected Lua development host failure\n", stderr);
        return 1;
    }
}
