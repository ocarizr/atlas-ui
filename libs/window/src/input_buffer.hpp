/** @file @brief Allocation-free fail-safe transport for native callbacks. */
#pragma once
#include <atlas/window/input.hpp>
#include <utility>

namespace atlas::window::detail
{
class input_buffer
{
  public:
    void push(input_event event) noexcept
    {
        if (batch_.overflow)
        {
            return;
        }

        if (batch_.count == batch_.events.size())
        {
            batch_.count = 1;
            batch_.events[0] = {input_kind::focus_lost};
            batch_.overflow = true;
            return;
        }

        batch_.events[batch_.count++] = event;
    } ///< Overflow discards incomplete transitions rather than silently losing a release.

    input_batch take() noexcept
    {
        return std::exchange(batch_, {});
    } ///< Explicit owner drain.
  private:
    input_batch batch_; ///< Fixed capacity, valid even inside a noexcept WNDPROC.
}; ///< No event handler, heap allocation or UI dependency.
} // namespace atlas::window::detail
