#pragma once

#include <cstdint>

namespace atlas::application
{
enum class ApplicationKind : std::uint8_t
{
    Game,
    Editor
};

enum class ToolKind : std::uint8_t
{
    Cook,
    Package
};

int run_application(ApplicationKind kind, int argc, char** argv);
int run_tool(ToolKind kind, int argc, char** argv);
} // namespace atlas::application
