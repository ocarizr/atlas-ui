/** @file @brief Shared executable composition for the game and editor graphics foundation. */
#pragma once
#include <atlas/application/entry_point.hpp>

namespace atlas::application
{
int run_renderer_host(application_kind kind, int argc,
                      char** argv); ///< Run graphics or a bounded smoke check.
}
