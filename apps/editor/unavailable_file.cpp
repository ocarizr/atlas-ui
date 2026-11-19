/** @file @brief SDK-free editor retains model tests without a filesystem backend. */
#include "document_file.hpp"

namespace atlas::editor
{
/// @brief Report unavailable native persistence without opening any source.
void write_editor_source(const std::filesystem::path&, std::string_view, bool)
{
    throw std::runtime_error("Editor byte storage is unavailable in this build");
}

/// @brief Match the native workspace adapter while retaining explicit unavailable outcomes.
document_storage make_document_storage(jobs::job_system& jobs, diagnostics::services sinks)
{
    return {[&jobs, sinks](const auto& path) -> std::optional<ui_content::document>
            {
                return open_document(path, jobs, sinks);
            },
            [sinks](const auto& path, const auto& source)
            {
                save_document(path, source, sinks);
            },
            [](const auto& path)
            {
                remove_document(path);
            }};
}

/// @brief No filesystem backend is available in the SDK-free profile.
void remove_document(const std::filesystem::path&)
{
    throw std::runtime_error("Editor document storage is unavailable in this build");
}

/// @brief No installed platform tools are available in the SDK-free profile.
std::filesystem::path installed_tools_root()
{
    return {};
}

/// @brief Report an unavailable platform filesystem service.
bool document_exists(const std::filesystem::path&)
{
    throw std::runtime_error("Editor document storage is unavailable in this build");
}

/// @brief Report an unavailable platform filesystem service.
std::string read_editor_source(const std::filesystem::path&, jobs::job_system&,
                               const diagnostics::services&)
{
    throw std::runtime_error("Editor document storage is unavailable in this build");
}

/// @brief Report an unavailable platform filesystem service.
ui_content::document open_document(const std::filesystem::path&, jobs::job_system&,
                                   const diagnostics::services&)
{
    throw std::runtime_error("Editor document storage is unavailable in this build");
}

/// @brief Report an unavailable platform filesystem service.
void save_document(const std::filesystem::path&, const ui_content::document&,
                   const diagnostics::services&)
{
    throw std::runtime_error("Editor document storage is unavailable in this build");
}
} // namespace atlas::editor
