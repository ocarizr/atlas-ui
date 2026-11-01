/**
 * @file
 * @brief Application/tool composition roots and bootstrap argument contracts.
 */

#pragma once

#include <cstdint>

namespace atlas::application
{
/// @brief Application composition root selected by the executable entry point.
enum class application_kind : std::uint8_t
{
    /// @brief Runtime application composition root; gameplay is outside the current bootstrap
    /// scope.
    game,
    /// @brief Editor application composition root sharing runtime libraries.
    editor
};

/// @brief Content-tool bootstrap selected by its executable entry point.
enum class tool_kind : std::uint8_t
{
    /// @brief Content cooking command bootstrap.
    cook,
    /// @brief Content packaging command bootstrap.
    package
};

/// @brief Compose the selected executable's services and return its bootstrap/smoke-test exit
/// status.
///
/// @param kind Runtime application or content-tool composition root.
/// @param argc Number of process arguments, including the executable name.
/// @param argv Borrowed process argument vector valid for the duration of this entry point.
int run_application(application_kind kind, int argc, char** argv);
/// @brief Validate tool arguments and report commands not implemented by the content-tool
/// bootstrap.
///
/// @param kind Runtime application or content-tool composition root.
/// @param argc Number of process arguments, including the executable name.
/// @param argv Borrowed process argument vector valid for the duration of this entry point.
int run_tool(tool_kind kind, int argc, char** argv);
} // namespace atlas::application
