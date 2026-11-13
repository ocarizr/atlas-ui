/** @file @brief Serial command admission implementation. */
#include "command_state.hpp"
#include <cmath>

namespace atlas::ui
{
namespace
{
/// @brief Reject invalid keys, types and values before changing reserved producer storage.
void validate(element_handle target, property field, const property_value& value,
              std::uint64_t producer)
{
    if (target.generation == 0 || producer == 0)
    {
        throw std::invalid_argument("UI command needs a target generation and producer identity");
    }

    if (field == property::visible)
    {
        if (!std::holds_alternative<bool>(value))
        {
            throw std::invalid_argument("UI visible requires a boolean");
        }

        return;
    }

    if ((field == property::width || field == property::height) &&
        std::holds_alternative<std::monostate>(value))
    {
        return;
    }

    const auto* scalar = std::get_if<float>(&value);
    if (!scalar || !valid_scalar(field, *scalar))
    {
        throw std::invalid_argument("UI property requires a finite scalar within its range");
    }
}
} // namespace

/// @brief Shared numeric domain for commands and track keys.
bool valid_scalar(property field, float value) noexcept
{
    if (field == property::visible || field > property::canvas_y_anchor_max ||
        !std::isfinite(value))
    {
        return false;
    }

    if ((field >= property::translate_x && field <= property::translate_y) ||
        field == property::rotate ||
        (field >= property::canvas_x_offset_min && field <= property::canvas_y_offset_max))
    {
        return true;
    }

    return value >= 0 &&
           ((field != property::opacity && field < property::canvas_x_anchor_min) || value <= 1);
}

/// @brief Index the closed command vocabulary without exposing its storage.
std::string_view property_name(property field) noexcept
{
    constexpr std::array<std::string_view, 26> names{"width",
                                                     "height",
                                                     "opacity",
                                                     "visible",
                                                     "translate-x",
                                                     "translate-y",
                                                     "scale-x",
                                                     "scale-y",
                                                     "rotate",
                                                     "margin-left",
                                                     "margin-top",
                                                     "margin-right",
                                                     "margin-bottom",
                                                     "padding-left",
                                                     "padding-top",
                                                     "padding-right",
                                                     "padding-bottom",
                                                     "spacing",
                                                     "canvas-x-offset-min",
                                                     "canvas-x-offset-max",
                                                     "canvas-y-offset-min",
                                                     "canvas-y-offset-max",
                                                     "canvas-x-anchor-min",
                                                     "canvas-x-anchor-max",
                                                     "canvas-y-anchor-min",
                                                     "canvas-y-anchor-max"};
    const auto index = static_cast<std::size_t>(field);
    static_assert(names.size() == static_cast<std::size_t>(property::canvas_y_anchor_max) + 1);
    return index < names.size() ? names[index] : "unknown";
}

/// @brief Null means the standard allocation resource; applications may inject their shared PMR.
command_buffer::command_buffer(command_limits limits,
                               std::shared_ptr<std::pmr::memory_resource> memory,
                               diagnostics::services destinations)
{
    if (limits.max_commands == 0 || limits.max_commands > 65536 || limits.max_batches == 0 ||
        limits.max_batches > 1024 || limits.max_warning_details > 64)
    {
        throw std::invalid_argument("UI command limits exceed supported bounds");
    }

    if (!memory)
    {
        memory = std::shared_ptr<std::pmr::memory_resource>(std::pmr::new_delete_resource(),
                                                            [](std::pmr::memory_resource*)
                                                            {
                                                            });
    }

    impl_ = std::make_unique<impl>(limits, std::move(memory), std::move(destinations));
}

/// @brief Bind the tree-owned producer to its session and borrowed reentrancy guard.
command_buffer::command_buffer(std::uint64_t session, const bool* tree_busy, command_limits limits,
                               std::shared_ptr<std::pmr::memory_resource> memory,
                               diagnostics::services destinations)
    : command_buffer(limits, std::move(memory), std::move(destinations))
{
    impl_->session = session;
    impl_->tree_busy = tree_busy;
}

/// @brief Published batches may outlive this owner; producer destruction remains owner-affine.
command_buffer::~command_buffer()
{
    if (impl_->owner != std::this_thread::get_id())
    {
        std::terminate();
    }
}

/// @brief New frames cannot silently discard pending dispatch work or reuse earlier identities.
void command_buffer::begin_frame(std::uint64_t frame)
{
    impl_->check(false);
    if (frame == 0 || frame <= impl_->frame)
    {
        throw std::invalid_argument("UI frame identity must increase");
    }

    if (impl_->sealed != impl_->history.size())
    {
        throw std::logic_error("seal or discard pending UI commands before beginning a frame");
    }

    impl_->history.clear();
    impl_->sealed = 0;
    impl_->ordinal = 0;
    impl_->frame = frame;
}

/// @brief Append is allocation-free after construction; serial order is the authoritative order.
void command_buffer::set(element_handle target, property field, property_value value,
                         std::uint64_t producer)
{
    impl_->check();
    validate(target, field, value, producer);
    if (impl_->session != 0 && target.session != impl_->session)
    {
        throw std::invalid_argument("UI command target belongs to another tree session");
    }

    impl_->require_capacity(1);
    impl_->history.push_back({target, field, value, producer, impl_->history.size() + 1});
}

/// @brief Check the remaining frame budget without changing pending or sealed history.
void command_buffer::require_capacity(std::size_t writes) const
{
    impl_->check();
    impl_->require_capacity(writes);
}

/// @brief Roll back an unsealed dispatch; keep earlier conflict history intact.
void command_buffer::discard_pending()
{
    impl_->check();
    impl_->history.resize(impl_->sealed);
}

/// @brief Producer observations obey the same ownership and reentrancy policy as admission.
std::size_t command_buffer::pending() const
{
    impl_->check();
    return impl_->history.size() - impl_->sealed;
}
} // namespace atlas::ui
