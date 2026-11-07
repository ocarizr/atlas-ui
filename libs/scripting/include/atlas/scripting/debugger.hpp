/**
 * @file
 * @brief Optional owner-thread development transport; no Lua or native socket types escape.
 */
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace atlas::scripting
{
/// @brief One bounded line receive without interpreting debugger messages in a worker.
struct debug_message
{
    /// @brief Distinguish complete data, a bounded poll and a disconnected peer.
    enum class status : std::uint8_t
    {
        ready,   ///< A complete protocol line is available.
        timeout, ///< No complete line arrived within the selected wait.
        closed   ///< The peer disconnected or the adapter was closed.
    }; ///< Transport outcome.
    status state = status::timeout; ///< No message by default.
    std::string line; ///< Complete line excluding its terminator; transport caps retained bytes.
};

/// @brief Development-only connection to a local debugger; methods run on the VM owner.
class debug_transport
{
  public:
    virtual ~debug_transport() = default; ///< Release native resources outside VM callbacks.
    virtual bool connect(std::uint16_t port) = 0;  ///< Connect only to loopback.
    virtual void send(std::string_view bytes) = 0; ///< Send bounded debugger protocol bytes.
    virtual debug_message receive(double timeout_seconds) = 0; ///< Bounded wait, including pauses.
    virtual void close() noexcept = 0; ///< Idempotent disconnect; never call Lua.
};

/// @brief Verified LuaPanda source is supplied by tools/caller, never opened by the runtime.
struct debugger_options
{
    std::string agent_source;                   ///< Pinned debugger source, at most 512 KiB.
    std::shared_ptr<debug_transport> transport; ///< Retained owner transport.
    std::uint16_t port = 8818; ///< Explicit per-VM endpoint; loopback is enforced by the adapter.
};
} // namespace atlas::scripting
