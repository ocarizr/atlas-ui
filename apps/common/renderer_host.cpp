/** @file @brief Owner-thread native loop, shared by editor and game composition roots. */
#include "renderer_host.hpp"
#include <atlas/diagnostics/logging.hpp>
#include <atlas/memory/memory_resource.hpp>
#include <atlas/renderer/renderer.hpp>
#include <atlas/window/window.hpp>
#include <string_view>

namespace atlas::application
{
/// @brief Signal and wait for all earlier commands on the graphics queue.
int run_renderer_host(application_kind kind, int argc, char** argv)
{
    if (argc == 2 && std::string_view{argv[1]} == "--smoke-test")
    {
        return run_application(kind, argc, argv);
    }

    diagnostics::logger log;
    const std::string_view argument = argc == 2 ? argv[1] : "";
    if (argc > 2 || (argument != "" && argument != "--warp" &&
                     argument != "--graphics-smoke-test" && argument != "--help"))
    {
        (void)log.write(diagnostics::log_level::error, "application",
                        "Unknown graphics argument; use --help");
        return 2;
    }

    if (argument == "--help")
    {
        (void)log.write(diagnostics::log_level::info, "application",
                        "Options: --warp, --smoke-test, --graphics-smoke-test");
        return 0;
    }

    try
    {
        const bool smoke = argument == "--graphics-smoke-test";
        const auto memory = memory::make_resource();
        window::window target{
            {kind == application_kind::game ? "Atlas Game" : "Atlas Editor", 960, 540, !smoke}};
        renderer::device_options options;
        if (smoke || argument == "--warp")
        {
            options.adapter = renderer::adapter_preference::software;
        }

        renderer::device graphics{options, memory};
        renderer::presentation_surface surface{graphics, target};
        const auto information = graphics.info();
        (void)log.write(diagnostics::log_level::info, "renderer",
                        information.adapter_name + (information.debug_layer_active
                                                        ? "; debug layer active"
                                                        : "; debug layer unavailable or disabled"));
        std::uint32_t frames = 0;
        for (;;)
        {
            const auto state = target.poll_events();
            if (state.close_requested)
            {
                break;
            }

            surface.resize({state.width, state.height});
            const auto result = surface.render_frame();
            if (result == renderer::frame_result::surface_closed)
            {
                break;
            }

            if (smoke && ++frames == 3)
            {
                break;
            }

            if (result == renderer::frame_result::suspended ||
                result == renderer::frame_result::occluded)
            {
                target.wait_for_events();
            }
        }

        graphics.wait_idle();
        if (graphics.debug_error_count() != 0)
        {
            throw std::runtime_error("native graphics validation reported errors");
        }

        return 0;
    }
    catch (const std::exception& error)
    {
        (void)log.write(diagnostics::log_level::error, "renderer", error.what());
        return 1;
    }
}
} // namespace atlas::application
