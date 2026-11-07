/** @file @brief Bounded Winsock transport for the local Lua development host. */
#include "transport.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace atlas::scripting::development
{
namespace
{
/// @brief Own one nonblocking socket and bounded partial protocol input.
class loopback_transport final : public debug_transport
{
  public:
    loopback_transport()
    {
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        {
            throw std::runtime_error("Lua debugger WSAStartup failed");
        }
    } ///< Initialize the SDK only in the native adapter.

    ~loopback_transport() override
    {
        close();
        WSACleanup();
    } ///< Socket closes before its Winsock reference is released.

    bool connect(std::uint16_t port) override
    {
        close();
        socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_ == INVALID_SOCKET)
        {
            return false;
        }

        u_long enabled = 1;
        if (ioctlsocket(socket_, FIONBIO, &enabled) != 0)
        {
            close();
            return false;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if (::connect(socket_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        {
            if (WSAGetLastError() != WSAEWOULDBLOCK || !wait(true, 0.25))
            {
                close();
                return false;
            }

            int error = 0;
            int size = sizeof(error);
            if (getsockopt(socket_, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &size) ||
                error)
            {
                close();
                return false;
            }
        }

        return true;
    } ///< Connect only to the caller-selected port on 127.0.0.1.

    void send(std::string_view bytes) override
    {
        if (bytes.size() > max_message || socket_ == INVALID_SOCKET)
        {
            throw std::runtime_error("Lua debugger disconnected or message too large");
        }

        while (!bytes.empty())
        {
            const auto count = ::send(socket_, bytes.data(), static_cast<int>(bytes.size()), 0);
            if (count > 0)
            {
                bytes.remove_prefix(static_cast<std::size_t>(count));
            }
            else if (count == 0 || WSAGetLastError() != WSAEWOULDBLOCK || !wait(true, 0.25))
            {
                throw std::runtime_error("Lua debugger send failed or timed out");
            }
        }
    } ///< Handle partial writes without blocking indefinitely.

    debug_message receive(double timeout) override
    {
        for (;;)
        {
            if (const auto newline = input_.find('\n'); newline != std::string::npos)
            {
                auto line = input_.substr(0, newline);
                input_.erase(0, newline + 1);
                return {debug_message::status::ready, std::move(line)};
            }

            if (socket_ == INVALID_SOCKET)
            {
                return {debug_message::status::closed, {}};
            }

            if (!wait(false, timeout))
            {
                return {debug_message::status::timeout, {}};
            }

            char bytes[4096];
            const auto count = recv(socket_, bytes, sizeof(bytes), 0);
            if (count <= 0)
            {
                close();
                return {debug_message::status::closed, {}};
            }

            if (input_.size() + static_cast<std::size_t>(count) > max_message)
            {
                close();
                throw std::runtime_error("Lua debugger input exceeded 1 MiB");
            }

            input_.append(bytes, static_cast<std::size_t>(count));
            timeout = 0;
        }
    } ///< Preserve incomplete lines across bounded nonblocking polls.

    void close() noexcept override
    {
        if (socket_ != INVALID_SOCKET)
        {
            closesocket(socket_);
            socket_ = INVALID_SOCKET;
        }

        input_.clear();
    } ///< Idempotent disconnect, including failed connections.

  private:
    static constexpr std::size_t max_message = 1048576; ///< Bound untrusted adapter input.
    SOCKET socket_ = INVALID_SOCKET; ///< Owned platform handle never crosses the portable API.
    std::string input_; ///< Partial line, capped independently of Lua allocation limits.

    bool wait(bool writing, double seconds)
    {
        if (!std::isfinite(seconds))
        {
            throw std::invalid_argument("Lua debugger timeout must be finite");
        }

        fd_set sockets{};
        FD_ZERO(&sockets);
        FD_SET(socket_, &sockets);
        timeval timeout{0, static_cast<long>(std::clamp(seconds, 0.0, 0.25) * 1000000)};
        return select(0, writing ? nullptr : &sockets, writing ? &sockets : nullptr, nullptr,
                      &timeout) > 0;
    } ///< The native wait never exceeds 250 milliseconds per operation.
};
} // namespace

/// @brief Keep Winsock lifetime and native storage behind the portable transport contract.
std::shared_ptr<debug_transport> make_loopback_transport()
{
    return std::make_shared<loopback_transport>();
}
} // namespace atlas::scripting::development
