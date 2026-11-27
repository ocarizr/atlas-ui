/** @file @brief Read user preferences via the streamer; write with durable atomic replacement. */
#include "theme_storage.hpp"

namespace atlas::editor
{
/// @brief Missing preferences retain the preset; invalid bytes are logged and never rewritten.
void open_theme_preferences(editor_theme& theme, jobs::job_system& jobs,
                            const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"editor.theme.open", "editor"}};
    std::filesystem::path path;
    try
    {
        path = editor_preferences_path();
        if (path.empty())
        {
            return;
        }

        if (document_exists(path))
        {
            theme.configure(parse_theme_preferences(read_editor_source(path, jobs, sinks)));
        }
    }
    catch (const std::exception& error)
    {
        diagnostics::write(sinks, diagnostics::log_level::warning, "editor.theme", error.what());
    }

    if (path.empty())
    {
        return;
    }

    theme.persist(
        [path, sinks](std::string_view bytes)
        {
            const diagnostics::operation_scope saving{sinks, {"editor.theme.save", "editor"}};
            try
            {
                std::filesystem::create_directories(path.parent_path());
                write_editor_source(path, bytes, true);
            }
            catch (const std::exception& error)
            {
                diagnostics::write(sinks, diagnostics::log_level::warning, "editor.theme",
                                   error.what());
                throw;
            }
        });
}
} // namespace atlas::editor
