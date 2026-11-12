/** @file @brief Load typed tool catalogs without passing filesystem paths to Lua or UI. */
#include "tool_registry.hpp"
#include <array>

namespace atlas::editor
{
/// @brief Mount and publish each catalog as a complete origin.
tool_registry::tool_registry(const tool_roots& roots, jobs::job_system& jobs,
                             const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"editor.tools.discover", "editor"}};
    const std::array origins{roots.editor, roots.project};
    for (std::size_t i = 0; i < origins.size(); ++i)
    {
        try
        {
            const auto& directory = origins[i];
            if (directory.empty() || !document_exists(directory / "catalog.xml"))
            {
                continue;
            }

            const auto catalog_path = std::filesystem::canonical(directory / "catalog.xml");
            if (catalog_path.parent_path() != std::filesystem::canonical(directory))
            {
                throw std::invalid_argument("Editor tool catalog escapes its authorized root");
            }

            const auto bytes = read_editor_source(catalog_path, jobs, sinks);
            const auto catalog = assets::read_xml({bytes, "editor tool catalog"}, sinks);
            const auto& records = catalog.document().records;
            if (records.size() > 128)
            {
                throw std::invalid_argument("Editor tool catalog exceeds 128 records");
            }

            const std::array authorized{streaming::source_root{"tools", directory}};
            auto storage = std::make_unique<streaming::catalog_streamer>(
                jobs, catalog, streaming::prepare_loose_mount(catalog, authorized, sinks),
                streaming::streamer_limits{}, nullptr, sinks);
            std::vector<tool_entry> found;
            std::optional<tool_entry> shell;
            for (const auto& record : records)
            {
                if (record.reference.type != assets::asset_type::ui)
                {
                    continue;
                }

                const auto name = std::filesystem::path{record.source.path}.stem().string();
                tool_entry entry{(i == 0 ? "Editor: " : "Project: ") + name, record.reference,
                                 storage.get()};
                if (i == 0 && record.source.path == "shell.xml")
                {
                    shell = std::move(entry);
                }
                else
                {
                    found.push_back(std::move(entry));
                }
            }

            if (found.size() > 16)
            {
                throw std::invalid_argument("Editor tool catalog exceeds 16 contributed panels");
            }

            tools_.reserve(tools_.size() + found.size());
            mounts_.push_back(std::move(storage));
            if (shell)
            {
                shell_ = std::move(shell);
            }

            for (auto& entry : found)
            {
                tools_.push_back(std::move(entry));
            }
        }
        catch (const std::exception& error)
        {
            diagnostics::write(sinks, diagnostics::log_level::warning, "editor.tools",
                               error.what());
        }
    }
}

/// @brief Only the installed origin may define the editor shell.
const std::optional<tool_entry>& tool_registry::shell() const noexcept
{
    return shell_;
}

/// @brief Stable origin and catalog ID order defines the selector cycle.
std::span<const tool_entry> tool_registry::tools() const noexcept
{
    return tools_;
}
} // namespace atlas::editor
