/** @file @brief SDK-free editor retains model tests without a filesystem backend. */
#include "document_file.hpp"

namespace atlas::editor
{
/// @brief Report an unavailable platform filesystem service.
bool document_exists(const std::filesystem::path&)
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
