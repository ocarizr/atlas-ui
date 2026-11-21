/** @file @brief Private owner state shared by editor presentation and document commands. */
#pragma once
#include "asset_panel.hpp"
#include "editor_view.hpp"
#include "flow_panel.hpp"
#include "panel_scene.hpp"
#include "tool_panel.hpp"

namespace atlas::editor
{
/// @brief Keep authored values, persistent file and render handoff on their proper owners.
class editor_view::impl
{
  public:
    /// @brief Borrow the application job pool and preserve the selected document path.
    impl(document_session& active, jobs::job_system& workers, std::filesystem::path path,
         const diagnostics::services& destinations, const tool_registry* sources)
        : legacy_session{&active}, jobs{workers}, file{std::move(path)}, sinks{destinations},
          layout{jobs.dispatcher(), {}, {}, sinks}, paint{jobs.dispatcher(), {}, {}, sinks},
          tools{sources}
    {
        if (tools)
        {
            try
            {
                if (const auto& definition = tools->shell())
                {
                    shell = std::make_unique<tool_panel>(*definition, true, sinks);
                }
                else
                {
                    status = "Recovery mode";
                }
            }
            catch (const std::exception& error)
            {
                diagnostics::write(sinks, diagnostics::log_level::warning, "editor.shell",
                                   error.what());
                status = "Recovery mode";
            }
        }
    }

    void execute(const control_action& action); ///< Run only outside Atlas event callbacks.
    void rebuild(ui::layout_rect viewport, float scale); ///< Publish complete chrome candidate.
    void compose_flow(panel_plan& plan,
                      ui::layout_rect bounds); ///< Graph tool and explicit mode switch.
    void cycle_tool(); ///< Switch to the next valid contribution or the preview.
    bool dispatch(std::span<const input::event> events); ///< Process isolated Lua and host actions.

    /// @brief Resolve the active owner only after pending UI routes finish.
    document_session& session()
    {
        return workspace ? workspace->active() : *legacy_session;
    }

    /// @brief Bind persistence to the same owner as the currently displayed history.
    const std::filesystem::path& path() const
    {
        return workspace ? workspace->path() : file;
    }

    bool execute_document(control_kind kind); ///< Handle document lifecycle commands.
    void new_session(); ///< Create a collision-checked source target beside the active document.

    document_session* legacy_session;        ///< Compatibility owner when no workspace is supplied.
    document_workspace* workspace = nullptr; ///< Application-owned sessions outlive this view.
    std::uint64_t next_document = 1;         ///< Monotonic candidate suffix for New.
    std::chrono::steady_clock::time_point next_autosave =
        std::chrono::steady_clock::now() + std::chrono::seconds{30}; ///< Owner checkpoint deadline.
    jobs::job_system& jobs;              ///< Used for native streamer reads and joined layout work.
    std::filesystem::path file;          ///< Explicit host-selected document file.
    diagnostics::services sinks;         ///< Logs and profile destinations.
    ui::layout_engine layout;            ///< Shared engine, reused by chrome and preview.
    ui::paint_engine paint;              ///< Shared ordered paint preparation.
    std::unique_ptr<panel_scene> chrome; ///< Cached controls and immutable packet.
    ui::layout_rect viewport;            ///< Last logical extent.
    float density = 0;                   ///< Last physical/logical ratio.
    std::size_t page = 0;                ///< Hierarchy page.
    std::string status;                  ///< Short ASCII result for the toolbar.
    const tool_registry* tools;          ///< Origin mounts outlive every panel.
    std::unique_ptr<tool_panel> shell;   ///< Authored editor chrome and its Lua behavior.
    bool shell_visible = false;          ///< Authored shell owns input only when it is arranged.
    std::unique_ptr<tool_panel> active_tool; ///< Current independent contribution.
    std::size_t tool_index = 0;              ///< Zero displays the document preview.
    std::unique_ptr<asset_panel>
        assets;                       ///< Browser state references the host's durable repository.
    ui::layout_rect tool_bounds;      ///< Last arranged contribution viewport.
    std::unique_ptr<flow_panel> flow; ///< Optional persistent Atlas graph tool.
};

} // namespace atlas::editor
