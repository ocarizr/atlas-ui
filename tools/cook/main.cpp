/**
 * @file
 * @brief Process entry point for tools/cook; libraries remain independent of application
 * composition.
 */

#include <atlas/application/entry_point.hpp>

/// @brief Forward process arguments into this executable's composition root and return its exit
/// status.
int main(int argc, char** argv)
{
    return atlas::application::run_tool(atlas::application::tool_kind::cook, argc, argv);
}
