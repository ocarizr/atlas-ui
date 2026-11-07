/** @file @brief Private portable factory for the Windows developer-only loopback adapter. */
#pragma once
#include <atlas/scripting/debugger.hpp>

namespace atlas::scripting::development
{
/// @brief Create an outbound IPv4 loopback transport; no listener or remote endpoint is exposed.
std::shared_ptr<debug_transport> make_loopback_transport();
} // namespace atlas::scripting::development
