/**
 * @file
 * @brief Streamer-buffer XML authoring and equivalent generated runtime factories. */
#pragma once
#include <atlas/mvvm/view_model.hpp>

namespace atlas::mvvm_content
{
/// @brief Bound XML/source processing separately from runtime dispatch budgets.
struct content_limits
{
    std::size_t max_bytes = 1048576; ///< Input/output XML bytes, at most 16777216.
    mvvm::schema_limits schema{};    ///< Definition/type/text bounds.
};

/// @brief Parse borrowed UTF-8 bytes supplied by the caller/streamer; never open a path.
mvvm::definition parse_xml(std::string_view bytes, content_limits limits = {},
                           const diagnostics::services& destinations = {});
/// @brief Return canonical authored XML for the editor persistence boundary.
std::string write_xml(const mvvm::definition& source, content_limits limits = {},
                      const diagnostics::services& destinations = {});
/// @brief Generate a C++20 factory in atlas::generated; function name must start with make_.
std::string generate_factory_cpp(const mvvm::definition& source, std::string_view function_name,
                                 content_limits limits = {},
                                 const diagnostics::services& destinations = {});
} // namespace atlas::mvvm_content
