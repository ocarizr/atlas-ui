/** @file @brief Main-owned window messages and application-owned persistent render coordination. */
#include "renderer_host.hpp"
#include "graphics_handler.hpp"
#include "input_adapter.hpp"
#include "ui_demo.hpp"
#include <algorithm>
#include <atlas/memory/memory_resource.hpp>
#include <string_view>
#include <thread>

namespace atlas::application
{
namespace
{
/// @brief Keep dispatching window messages during full queues, initialization and render teardown.
void run_frames(window::window& target, render_host::render_loop& loop, bool smoke,
                const presentation_feedback& presented, const diagnostics::services& destinations,
                ui_demo& ui)
{
    const auto pump = [&]
    {
        target.poll_events();
        target.wait_for_events(1);
    };
    try
    {
        std::uint64_t next_id = 1;
        const auto started = std::chrono::steady_clock::now();
        while (!loop.finished())
        {
            const auto state = target.poll_events();
            if (state.close_requested ||
                (smoke && presented.presented.load(std::memory_order_relaxed) >= 3))
            {
                break;
            }

            const auto native_input = target.take_input();
            if (native_input.overflow)
            {
                diagnostics::write(destinations, diagnostics::log_level::warning,
                                   "application.input",
                                   "native input overflow; logical ownership reset");
            }

            if (loop.try_submit({next_id, state, ui.snapshot(state, native_input.values())}))
            {
                ++next_id;
            }

            const bool inactive =
                state.minimized || presented.result.load(std::memory_order_relaxed) ==
                                       renderer::frame_result::occluded;
            target.wait_for_events(inactive ? 16U : 1U);
            if (smoke && std::chrono::steady_clock::now() - started > std::chrono::seconds{10})
            {
                throw std::runtime_error("threaded graphics smoke test timed out");
            }
        }

        loop.stop_and_join(pump);
        ui.shutdown();
    }
    catch (...)
    {
        const auto original = std::current_exception();
        try
        {
            loop.stop_and_join(pump);
        }
        catch (...)
        {
            diagnostics::write(destinations, diagnostics::log_level::error, "render_host",
                               "Render cleanup also failed; preserving the original exception");
        }

        try
        {
            ui.shutdown();
        }
        catch (...)
        {
            diagnostics::write(destinations, diagnostics::log_level::error, "application.input",
                               "UI cleanup also failed; preserving the original exception");
        }

        std::rethrow_exception(original);
    }
}
} // namespace

/// @brief Compose the single application pool and window before starting the dedicated GPU owner.
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
        const auto lease = target.share_presentation();
        jobs::job_system_options options;
        options.worker_count = std::clamp(std::thread::hardware_concurrency(), 1U, 4U);
        options.scratch.upstream = memory.get();
        jobs::job_system workers{options};
        const auto content_root = std::filesystem::absolute(argv[0]).parent_path() / "content";
        streaming::asset_streamer streamer{
            workers,
            {{"renderer.checker", streaming::loose_file_asset{content_root / "checker.bmp"}}},
            {},
            memory,
            {log, {}}};
        ui_images::image_cache images{
            streamer, renderer_content::make_image_decoder(), {}, memory, {log, {}}};
        const auto image = images.request({"renderer.checker", 0, ATLAS_DEMO_IMAGE_SIZE});
        ui_demo ui{workers.dispatcher(), memory, {log, {}}, images, image};
        presentation_feedback presented;
        render_host::render_loop loop{[&]
                                      {
                                          return std::make_unique<graphics_handler>(
                                              workers, lease, smoke || argument == "--warp", memory,
                                              diagnostics::services{log, {}}, presented);
                                      },
                                      {},
                                      {log, {}}};
        run_frames(target, loop, smoke, presented, {log, {}}, ui);
        streamer.shutdown();
        return 0;
    }
    catch (const std::exception& error)
    {
        (void)log->write(diagnostics::log_level::error, "renderer", error.what());
        return 1;
    }
}
} // namespace atlas::application
