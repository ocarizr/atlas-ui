/** @file @brief Real LuaPanda source runs in the embedded C++ Lua VM without application
 * privileges. */
#include <algorithm>
#pragma once
#include <atlas/scripting/session.hpp>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>

namespace atlas::scripting::tests
{
/// @brief Deterministic protocol peer; no socket or separate thread enters the VM.
class debug_peer final : public debug_transport
{
  public:
    bool closed = false;               ///< Observable teardown.
    bool available = true;             ///< Whether the local adapter accepts a connection.
    std::vector<std::string> sent;     ///< Complete protocol messages from the real agent.
    std::vector<std::string> messages; ///< Ordered inbound commands.
    std::size_t cursor = 0;            ///< Next command to accept.
    std::size_t gate = 999;            ///< Hold later commands until a breakpoint notification.

    bool connect(std::uint16_t) override
    {
        closed = false;
        return available;
    } ///< Local test endpoint.

    void send(std::string_view bytes) override
    {
        sent.emplace_back(bytes);
    } ///< Retain output.

    debug_message receive(double) override
    {
        if (cursor == gate && std::none_of(sent.begin(), sent.end(),
                                           [](const auto& message)
                                           {
                                               return message.find("stopOnBreakpoint") !=
                                                      std::string::npos;
                                           }))
        {
            return {debug_message::status::timeout, {}};
        }

        if (cursor < messages.size())
        {
            return {debug_message::status::ready, messages[cursor++]};
        }

        return {debug_message::status::closed, {}};
    } ///< A finite scripted peer never hangs on missing commands.

    void close() noexcept override
    {
        closed = true;
    } ///< Idempotent test disconnect.
};

/// @brief Read pinned debugger tooling as a test fixture, outside application storage contracts.
inline std::string agent_source()
{
    std::ifstream input{ATLAS_LUAPANDA_FIXTURE, std::ios::binary};
    if (!input)
    {
        throw std::runtime_error("run python -m scripts.dev.setup_lua --tools luapanda");
    }

    return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
}

} // namespace atlas::scripting::tests
