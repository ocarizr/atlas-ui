/** @file @brief Checked source/device/control state and whole-batch admission simulation. */
#include "input_state.hpp"

namespace atlas::input
{
namespace
{
/// @brief Primary sort groups controls so each binding visits only matching device instances.
auto control_key(const control_record& value)
{
    return std::tuple{value.device.kind, value.code, value.source, value.device.id};
}
} // namespace

/// @brief The same bounded kernel validates scratch and updates admitted state.
void input_system::impl::apply_to(const event& value, std::pmr::vector<control_record>& states,
                                  std::pmr::vector<device_record>& instances)
{
    const auto source_matches = [&](std::uint64_t source)
    {
        return !value.source || value.source == source;
    };
    if (value.kind == event_kind::focus_lost || value.kind == event_kind::source_reset)
    {
        std::erase_if(states,
                      [&](const auto& c)
                      {
                          return source_matches(c.source);
                      });
        std::erase_if(instances,
                      [&](const auto& d)
                      {
                          return source_matches(d.source);
                      });
        return;
    }

    if (value.kind == event_kind::device_disconnected || value.kind == event_kind::pointer_cancel)
    {
        std::erase_if(states,
                      [&](const auto& c)
                      {
                          return c.source == value.source && c.device.id == value.device.id;
                      });
        if (value.kind == event_kind::device_disconnected)
        {
            std::erase_if(instances,
                          [&](const auto& d)
                          {
                              return d.source == value.source && d.device.id == value.device.id;
                          });
        }

        return;
    }

    if (value.kind == event_kind::pointer_leave)
    {
        return;
    }

    auto device =
        std::find_if(instances.begin(), instances.end(),
                     [&](const auto& d)
                     {
                         return d.source == value.source && d.device.id == value.device.id;
                     });
    if (device != instances.end() && device->device.kind != value.device.kind)
    {
        throw std::invalid_argument("input device identity changed family without disconnection");
    }

    if (device == instances.end())
    {
        if (instances.size() == limits.max_devices)
        {
            throw std::overflow_error("input device capacity exhausted");
        }

        instances.push_back({value.source, value.device});
    }

    if (value.kind == event_kind::device_connected)
    {
        return;
    }

    control_record sample{value.source, value.device, value.control, value.value, epoch};
    const auto it = std::lower_bound(states.begin(), states.end(), sample,
                                     [](const auto& a, const auto& b)
                                     {
                                         return control_key(a) < control_key(b);
                                     });
    if (it == states.end() || control_key(*it) != control_key(sample))
    {
        if (states.size() == limits.max_controls)
        {
            throw std::overflow_error("input control capacity exhausted");
        }

        states.insert(it, sample);
    }
    else
    {
        it->value = value.value;
        if (value.value == input_value{})
        {
            it->neutral = epoch;
        }
    }
}

/// @brief Invalid metadata/capacity never reaches a raw or semantic callback.
void input_system::impl::preflight(std::span<const event> values)
{
    scratch_controls = controls;
    scratch_devices = devices;
    for (const auto& value : values)
    {
        validate_value(value.value);
        validate_value(value.position);
        if (value.kind > event_kind::device_disconnected ||
            value.device.kind > device_kind::gamepad)
        {
            throw std::invalid_argument("invalid input event category");
        }

        const bool reset =
            value.kind == event_kind::focus_lost || value.kind == event_kind::source_reset;
        if (!reset && !value.device.id)
        {
            throw std::invalid_argument("input event needs a device identity");
        }

        if ((value.kind == event_kind::control_changed ||
             value.kind == event_kind::pointer_motion) &&
            !value.control)
        {
            throw std::invalid_argument("input sample needs a control identity");
        }

        if ((value.kind == event_kind::pointer_motion || value.kind == event_kind::pointer_cancel ||
             value.kind == event_kind::pointer_leave) &&
            value.device.kind != device_kind::pointer)
        {
            throw std::invalid_argument("pointer event needs a pointer device");
        }

        if (value.kind == event_kind::pointer_motion && value.control != pointer_control::position)
        {
            throw std::invalid_argument("pointer motion needs the position control");
        }

        apply_to(value, scratch_controls, scratch_devices);
    }
}

/// @brief Neutral epochs advance with admitted event order, never repeat-key arrival timing.
void input_system::impl::apply(const event& value)
{
    ++epoch;
    apply_to(value, controls, devices);
}
} // namespace atlas::input
