/** @file @brief Main-owned editor window, storage and joined render-thread lifecycle. */
#include "editor_host.hpp"
#include "asset_files.hpp"
#include "document_file.hpp"
#include "editor_graphics.hpp"
#include "flow_project.hpp"
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
    std::vector<std::filesystem::path>
        files;            ///< Selected files, each owning an independent session.
    bool recover = false; ///< Restore the active document's checkpoint only by explicit request.
    std::filesystem::path asset_root;              ///< Optional explicit editor content origin.
    std::filesystem::path flow;                    ///< Independent graph authoring target.
    std::optional<assets::asset_ref> flow_actions; ///< Optional typed callable entrypoint.
    bool recover_flow =
        false;          ///< Explicit graph checkpoint restoration, independent of UI recovery.
    bool warp = false;  ///< Force software rendering.
    bool smoke = false; ///< Hidden bounded native render exercise.
    bool help = false;  ///< Print command-line usage.
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
            options.files.emplace_back(argv[++i]);
        }
        else if (argument == "--recover")
        {
            options.recover = true;
        }
        else if (argument == "--asset-root" && i + 1 < argc)
        {
            options.asset_root = argv[++i];
        }
        else if (argument == "--flow" && i + 1 < argc)
        {
            options.flow = argv[++i];
        }
        else if (argument == "--recover-flow")
        {
            options.recover_flow = true;
        }
        else if (argument == "--flow-actions" && i + 1 < argc)
        {
            options.flow_actions =
                assets::asset_ref{assets::parse_id(argv[++i]), assets::asset_type::script};
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

    if (options.files.empty())
    {
        options.files.emplace_back("atlas-ui.xml");
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
        auto close_retry = start;
        bool pending_close = false;
        while (!loop.finished())
        {
            const auto state = window.poll_events();
            const auto now = std::chrono::steady_clock::now();
            view.autosave(now);
            pending_close |= state.close_requested;
            const bool closing =
                pending_close || (smoke && presented.load(std::memory_order_relaxed) >= 3);
            if (closing && now >= close_retry)
            {
                if (smoke || view.preserve())
                {
                    break;
                }

                close_retry = now + std::chrono::seconds{1};
                window.cancel_close();
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
        if (!smoke)
        {
            (void)view.preserve();
        }

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

/// @brief Keep native window and render-loop lifetimes inside one joined presentation scope.
void present_editor(editor_view& view, jobs::job_system& workers, const launch_options& options,
                    const diagnostics::services& sinks)
{
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
    const auto asset_root = std::filesystem::absolute(
        options.asset_root.empty() ? options.files.back().parent_path() / "editor_assets"
                                   : options.asset_root);
    std::unique_ptr<asset_repository> repository;
    std::shared_ptr<streaming::catalog_streamer> preview_sources;
    if (!options.smoke)
    {
        repository = open_asset_repository(asset_root, workers, sinks);
        try
        {
            preview_sources =
                mount_editor_assets(repository->snapshot(), asset_root, workers, sinks);
        }
        catch (const std::exception& error)
        {
            diagnostics::write(sinks, diagnostics::log_level::warning, "editor.assets",
                               error.what());
        }
    }

    if (!preview_sources)
    {
        preview_sources = std::make_shared<streaming::catalog_streamer>(
            workers, catalog, streaming::storage_mount{}, streaming::streamer_limits{}, memory,
            sinks);
    }

    auto storage = make_document_storage(workers, sinks);
    if (options.smoke)
    {
        storage.read = [](const auto&) -> std::optional<ui_content::document>
        {
            return {};
        };
    }

    document_workspace workspace{{*preview_sources, {}, memory, sinks}, std::move(storage)};
    for (const auto& file : options.files)
    {
        const auto absolute = std::filesystem::absolute(file);
        if (options.recover)
        {
            (void)workspace.open_recovery(absolute);
        }
        else
        {
            (void)workspace.open(absolute);
        }
    }

    const auto path = workspace.path();
    const tool_registry tools{{installed_tools_root(), options.smoke
                                                           ? std::filesystem::path{}
                                                           : path.parent_path() / "editor_tools"},
                              workers,
                              sinks};
    std::unique_ptr<editor_flow::persistence> flow;
    if (!options.smoke)
    {
        flow = open_flow_document(
            std::filesystem::absolute(options.flow.empty() ? path.parent_path() / "atlas-flow.xml"
                                                           : options.flow),
            workers, sinks, options.recover_flow);
    }

    editor_view view{workspace, workers, sinks, &tools};
    if (repository)
    {
        view.bind_assets(*repository, asset_root);
    }

    if (flow)
    {
        view.bind_flow(*flow, repository.get(), memory);
    }

    if (options.flow_actions && repository)
    {
        load_flow_module(view, *options.flow_actions, repository->snapshot(), *preview_sources);
    }

    present_editor(view, workers, options, sinks);
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
            (void)log->write(diagnostics::log_level::info, "editor",
                             "Options: --document <file> (repeatable), --asset-root <directory>, "
                             "--flow <file>, --recover-flow, --flow-actions <script-asset-id>, "
                             "--recover, --warp, --smoke-test, "
                             "--graphics-smoke-test");
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
