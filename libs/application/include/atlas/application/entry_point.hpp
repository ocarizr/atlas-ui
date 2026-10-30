#pragma once

#include <cstdint>

namespace atlas::application
{
enum class application_kind : std::uint8_t
{
    game,
    editor
};

enum class tool_kind : std::uint8_t
{
    cook,
    package
};

int run_application(application_kind kind, int argc, char** argv);
int run_tool(tool_kind kind, int argc, char** argv);
} // namespace atlas::application
