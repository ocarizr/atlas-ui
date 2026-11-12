/**
 * @file
 * @brief Process entry point for apps/editor; libraries remain independent of application
 * composition.
 */

#include "editor_host.hpp"

/// @brief Forward process arguments into this executable's composition root and return its exit
/// status.
int main(int argc, char** argv)
{
    return atlas::editor::run_editor(argc, argv);
}
