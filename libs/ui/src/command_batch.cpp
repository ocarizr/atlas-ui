/** @file @brief Read-only packet queries do not reference mutable producer state. */
#include "command_state.hpp"

namespace atlas::ui
{
/// @brief Tree binding survives producer destruction through the immutable payload.
std::uint64_t command_batch::session() const noexcept
{
    return data_ ? data_->session : 0;
}

/// @brief Share only already prepared data and retain its allocation resource.
command_batch::command_batch(std::shared_ptr<const detail::command_data> data)
    : data_(std::move(data))
{
}

/// @brief A moved-from packet has no frame identity.
std::uint64_t command_batch::frame() const noexcept
{
    return data_ ? data_->frame : 0;
}

/// @brief A moved-from packet has no batch identity.
std::uint32_t command_batch::ordinal() const noexcept
{
    return data_ ? data_->ordinal : 0;
}

/// @brief Borrow winners only while this packet or another retained copy remains alive.
std::span<const property_write> command_batch::writes() const noexcept
{
    return data_ ? std::span<const property_write>{data_->writes}
                 : std::span<const property_write>{};
}

/// @brief Keep full provenance available even when logging is disabled or drops records.
std::span<const command_conflict> command_batch::conflicts() const noexcept
{
    return data_ ? std::span<const command_conflict>{data_->conflicts}
                 : std::span<const command_conflict>{};
}
} // namespace atlas::ui
