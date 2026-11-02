/** @file @brief Owner-thread native loop, shared by editor and game composition roots. */
#include "renderer_host.hpp"
#include "primitive_scene.hpp"
#include <algorithm>
#include <atlas/diagnostics/logging.hpp>
#include <atlas/jobs/job_system.hpp>
#include <atlas/memory/memory_resource.hpp>
#include <atlas/renderer/passes.hpp>
#include <atlas/renderer_jobs/executor.hpp>
#include <atlas/window/window.hpp>
#include <string_view>
#include <thread>

namespace atlas::application
{
namespace
{
/// @brief Poll native events and present bounded frames while content progresses asynchronously.
void run_frames(window::window& target, renderer::presentation_surface& surface,
                primitive_scene& scene, bool smoke)
{
    std::uint32_t frames = 0;
    const auto started = std::chrono::steady_clock::now();
    for (;;)
    {
        const auto state = target.poll_events();
        if (state.close_requested)
        {
            break;
        }

        surface.resize({state.width, state.height});
        auto result = renderer::frame_result::suspended;
        if (state.width != 0 && state.height != 0 && !state.minimized)
        {
            result = scene.render(surface, {state.width, state.height}, frames);
            ++frames;
        }

        if (result == renderer::frame_result::surface_closed)
        {
            break;
        }

        if (smoke && frames >= 3 && scene.content_presented())
        {
            break;
        }

        if (smoke && std::chrono::steady_clock::now() - started > std::chrono::seconds{10})
        {
            throw std::runtime_error("streamed graphics smoke test timed out");
        }

        if (result == renderer::frame_result::suspended ||
            result == renderer::frame_result::occluded)
        {
            target.wait_for_events();
        }
    }
}
} // namespace

/// @brief Compose shared services and run the owner-thread graphics loop or bounded smoke check.
int run_renderer_host(application_kind kind, int argc, char** argv)
{
    if (argc == 2 && std::string_view{argv[1]} == "--smoke-test")
    {
        return run_application(kind, argc, argv);
    }

    auto log = std::make_shared<diagnostics::logger>();
    const std::string_view argument = argc == 2 ? argv[1] : "";
    if (argc > 2 || (argument != "" && argument != "--warp" &&
                     argument != "--graphics-smoke-test" && argument != "--help"))
    {
        (void)log->write(diagnostics::log_level::error, "application",
                         "Unknown graphics argument; use --help");
        return 2;
    }

    if (argument == "--help")
    {
        (void)log->write(diagnostics::log_level::info, "application",
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

        renderer::device graphics{options, memory, {log, {}}};
        renderer::presentation_surface surface{graphics, target};
        jobs::job_system_options job_options;
        job_options.worker_count = std::clamp(std::thread::hardware_concurrency(), 1U, 4U);
        job_options.scratch.upstream = memory.get();
        jobs::job_system workers{job_options};
        renderer_jobs::executor recording{workers.dispatcher()};
        const auto content_root = std::filesystem::absolute(argv[0]).parent_path() / "content";
        streaming::asset_streamer streamer{
            workers,
            {{"renderer.checker", streaming::loose_file_asset{content_root / "checker.bmp"}},
             {"renderer.quad", streaming::loose_file_asset{content_root / "quad.hlsl"}}},
            {},
            memory,
            {log, {}}};
        renderer_content::content_loader content{
            streamer, renderer_content::make_image_decoder(), memory, {log, {}}};
        primitive_scene scene{graphics, recording, content, {log, {}}};
        const auto information = graphics.info();
        (void)log->write(diagnostics::log_level::info, "renderer",
                         information.adapter_name +
                             (information.debug_layer_active
                                  ? "; debug layer active"
                                  : "; debug layer unavailable or disabled"));
        run_frames(target, surface, scene, smoke);

        graphics.wait_idle();
        if (graphics.debug_error_count() != 0)
        {
            throw std::runtime_error("native graphics validation reported errors");
        }

        return 0;
    }
    catch (const std::exception& error)
    {
        (void)log->write(diagnostics::log_level::error, "renderer", error.what());
        return 1;
    }
}
} // namespace atlas::application
