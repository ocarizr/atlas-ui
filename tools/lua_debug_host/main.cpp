/** @file @brief Developer fixture host for debugging the actual embedded Lua C++ interpreter. */
#include "transport.hpp"
#include <atlas/scripting/domain.hpp>
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

/// @brief Execute a fixture with a distinct local value so the debugger can identify its target.
void tick(atlas::scripting::domain& owner, atlas::scripting::instance_handle instance,
          std::int64_t seed)
{
    const atlas::mvvm::value argument{seed};
    owner.call(instance, "tick", {&argument, 1});
}

/// @brief Exercise separate domain lifetimes and two instances in the shared UI interpreter.
void run_domains(std::string_view target, const atlas::scripting::source_asset& source,
                 atlas::scripting::debugger_options options)
{
    if (target != "ui" && target != "application")
    {
        throw std::invalid_argument("Debugger domain must be ui or application");
    }

    const auto schema = atlas::mvvm::compile({"debug_fixture"});
    auto ui_model = std::make_shared<atlas::mvvm::view_model>(schema);
    auto app_model = std::make_shared<atlas::mvvm::view_model>(schema);
    atlas::scripting::domain ui;
    atlas::scripting::domain application;
    for (auto* owner : {&ui, &application})
    {
        owner->register_source(source);
        owner->register_source({"entry", 1, "=atlas-debug-host", "return require('fixture')"});
    }

    ui.bind_context("view", ui_model);
    application.bind_context("state", app_model);
    const auto menu = ui.create({"entry", "view"});
    const auto sibling = ui.create({"entry", "view"});
    const auto app = application.create({"entry", "state"});
    const auto target_ui = target == "ui";
    auto& selected = target_ui ? ui : application;
    const auto instance = target_ui ? menu : app;
    selected.attach_debugger(instance, std::move(options));

    ui_model->begin_frame(1);
    app_model->begin_frame(1);
    ui.begin_frame(1);
    application.begin_frame(1);

    // Both untargeted instances execute the same catalog module while the debugger is attached.
    tick(ui, sibling, 101);
    std::cout << "Completed UI sibling tick\n";
    if (target_ui)
    {
        tick(application, app, 71);
        std::cout << "Completed application peer tick\n";
    }
    else
    {
        tick(ui, menu, 41);
        std::cout << "Completed UI peer tick\n";
    }

    tick(selected, instance, target_ui ? 41 : 71);
    selected.detach_debugger(instance);
    std::cout << "Completed " << target << " domain tick\n";
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
        if (argc != 4 && argc != 5)
        {
            throw std::invalid_argument(
                "Usage: atlas_lua_debug_host <script.lua> <LuaPanda.lua> <port> [ui|application]");
        }

        const auto port = std::stoul(argv[3]);
        if (!port || port > 65535)
        {
            throw std::invalid_argument("Debugger port must be 1..65535");
        }

        const auto source =
            "@" + std::filesystem::absolute(argv[1]).lexically_normal().generic_string();
        atlas::scripting::debugger_options options{
            read_fixture(argv[2]), atlas::scripting::development::make_loopback_transport(),
            static_cast<std::uint16_t>(port)};
        if (argc == 5)
        {
            run_domains(argv[4], {"fixture", 1, source, read_fixture(argv[1])}, std::move(options));
            return 0;
        }

        auto model =
            std::make_shared<atlas::mvvm::view_model>(atlas::mvvm::compile({"debug_fixture"}));
        atlas::scripting::session behavior{model};
        behavior.load(read_fixture(argv[1]), source);
        behavior.attach_debugger(std::move(options));
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
