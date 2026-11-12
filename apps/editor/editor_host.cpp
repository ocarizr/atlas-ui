/** @file @brief Main-owned editor window, storage and joined render-thread lifecycle. */
#include "editor_host.hpp"
#include "document_file.hpp"
#include "editor_graphics.hpp"
#include "tool_registry.hpp"
#include <algorithm>
#include <atlas/application/entry_point.hpp>
#include <atlas/memory/memory_resource.hpp>
#include <thread>

namespace atlas::editor
{
namespace
{
/// @brief Explicit document target and graphics preferences.
struct launch_options
{
    std::filesystem::path file = "atlas-ui.xml"; ///< Existing file to open or future save target.
    bool warp = false;                           ///< Force software rendering.
    bool smoke = false;                          ///< Hidden bounded native render exercise.
    bool help = false;                           ///< Print command-line usage.
};

/// @brief Parse editor options before touching native graphics or storage.
launch_options parse_options(int argc, char** argv)
{
    launch_options options;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view argument = argv[i];
        if (argument == "--document" && i + 1 < argc)
        {
            options.file = argv[++i];
        }
        else if (argument == "--warp")
        {
            options.warp = true;
        }
        else if (argument == "--graphics-smoke-test")
        {
            options.smoke = true;
        }
        else if (argument == "--help")
        {
            options.help = true;
        }
        else
        {
            throw std::invalid_argument("Unknown editor option; use --help");
        }
    }

    return options;
}

/// @brief Keep pumping native messages during rendering and shutdown.
void render_frames(window::window& window, editor_view& view, render_host::render_loop& loop,
                   const std::atomic<std::uint64_t>& presented, bool smoke,
                   const diagnostics::services& sinks)
{
    const auto pump = [&]
    {
        window.poll_events();
        window.wait_for_events(1);
    };
    try
    {
        std::uint64_t frame = 1;
        const auto start = std::chrono::steady_clock::now();
        while (!loop.finished())
        {
            const auto state = window.poll_events();
            if (state.close_requested || (smoke && presented.load(std::memory_order_relaxed) >= 3))
            {
                break;
            }

            const auto input = window.take_input();
            if (!state.minimized && state.width != 0 && state.height != 0)
            {
                if (loop.try_submit({frame, state, view.snapshot(state, input.values())}))
                {
                    ++frame;
                }
            }

            window.wait_for_events(state.minimized ? 16U : 1U);
            if (smoke && std::chrono::steady_clock::now() - start > std::chrono::seconds{10})
            {
                throw std::runtime_error("Editor graphics smoke test timed out");
            }
        }
    }
    catch (...)
    {
        const auto error = std::current_exception();
        try
        {
            loop.stop_and_join(pump);
        }
        catch (...)
        {
            diagnostics::write(sinks, diagnostics::log_level::error, "editor",
                               "Render cleanup also failed while handling the original error");
        }

        std::rethrow_exception(error);
    }

    loop.stop_and_join(pump);
}

/// @brief Mount the editor's preview catalog and compose one application-owned worker pool.
void run(const launch_options& options, const diagnostics::services& sinks)
{
    const auto memory = memory::make_resource();
    jobs::job_system_options policy;
    policy.worker_count = std::clamp(std::thread::hardware_concurrency(), 1U, 4U);
    policy.scratch.upstream = memory.get();
    jobs::job_system workers{policy};
    auto catalog =
        assets::catalog::compile({assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23")});
    streaming::catalog_streamer preview_sources{workers, catalog, {}, {}, memory, sinks};
    auto path = std::filesystem::absolute(options.file);
    const bool exists = !options.smoke && document_exists(path);
    auto source = exists ? open_document(path, workers, sinks) : new_document();
    document_session session{std::move(source), {preview_sources, {}, memory, sinks}};
    if (exists)
    {
        session.mark_saved();
    }

    const tool_registry tools{{installed_tools_root(), options.smoke
                                                           ? std::filesystem::path{}
                                                           : path.parent_path() / "editor_tools"},
                              workers,
                              sinks};
    editor_view view{session, workers, std::move(path), sinks, &tools};
    window::window window{{"Atlas Editor", 960, 540, !options.smoke}};
    const auto target = window.share_presentation();
    std::atomic<std::uint64_t> presented{0};
    render_host::render_loop loop{[&]
                                  {
                                      return std::make_unique<editor_graphics>(
                                          target, workers, options.warp || options.smoke, sinks,
                                          presented);
                                  },
                                  {},
                                  sinks};
    render_frames(window, view, loop, presented, options.smoke, sinks);
}
} // namespace

/// @brief Preserve the existing process smoke contract and launch the editor by default.
int run_editor(int argc, char** argv)
{
    if (argc == 2 && std::string_view{argv[1]} == "--smoke-test")
    {
        return application::run_application(application::application_kind::editor, argc, argv);
    }

    auto log = std::make_shared<diagnostics::logger>();
    try
    {
        const auto options = parse_options(argc, argv);
        if (options.help)
        {
            (void)log->write(
                diagnostics::log_level::info, "editor",
                "Options: --document <file>, --warp, --smoke-test, --graphics-smoke-test");
            return 0;
        }

        run(options, {log, {}});
        return 0;
    }
    catch (const std::exception& error)
    {
        (void)log->write(diagnostics::log_level::error, "editor", error.what());
        return 1;
    }
}
} // namespace atlas::editor
