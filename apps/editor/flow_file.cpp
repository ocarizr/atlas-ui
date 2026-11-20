/** @file @brief Graph files share streamer acquisition and durable native replacement. */
#include "flow_file.hpp"
#include "document_file.hpp"

namespace atlas::editor
{
/// @brief Native implementation remains selected through existing platform-specific file sources.
editor_flow::storage make_flow_storage(jobs::job_system& jobs, diagnostics::services sinks)
{
    return {[&jobs, sinks = std::move(sinks)](
                const std::filesystem::path& path) -> std::optional<std::string>
            {
                return document_exists(path) ? std::optional{read_editor_source(path, jobs, sinks)}
                                             : std::nullopt;
            },
            [](const std::filesystem::path& path, std::string_view bytes)
            {
                write_editor_source(path, bytes);
            },
            [](const std::filesystem::path& path)
            {
                remove_document(path);
            }};
}
} // namespace atlas::editor
