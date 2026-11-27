/** @file @brief Customize semantic chrome colors without editing the preview document. */
#include "theme_panel.hpp"
#include <algorithm>

namespace atlas::editor
{
/// @brief Preset selection and explicit reset controls replace only the inspector page.
void theme_panel::append(panel_plan& plan, const editor_theme& theme)
{
    const auto area = plan.inspector;
    if (area.width < 80 || area.height < 230)
    {
        return;
    }

    const auto add = [&](float offset, std::string label, std::string action)
    {
        plan.controls.push_back({{area.x + 8, area.y + offset, area.width - 16, 24},
                                 {.14F, .21F, .30F, 1},
                                 std::move(label),
                                 {control_kind::theme, std::move(action)}});
    };
    if (!visible)
    {
        add(area.height - 58, "Theme...", "toggle");
        return;
    }

    std::erase_if(plan.controls,
                  [&](const auto& item)
                  {
                      return item.bounds.x >= area.x && item.bounds.x < area.x + area.width &&
                             item.bounds.y >= area.y && item.bounds.y < area.y + area.height;
                  });
    const auto selected = theme.context()->snapshot();
    const auto& color = selected->definition().colors.at(key_);
    add(4, "Theme | Back", "toggle");
    add(32, "Preset: " + theme.preferences().preset + " | Switch", "preset");
    add(60, "Swatch: " + color.key + " | Next", "next");
    add(88, editing_ ? std::string{draft_.value()} + " |" : ui_content::format_color(color.value),
        "edit");
    add(116, "Apply color", "apply");
    add(144, "Reset swatch", "reset");
    add(172, "Reset all overrides", "reset-all");
    add(200, status_.empty() ? "Hex: #RRGGBBAA" : status_, "none");
}

/// @brief Validate and persist before accepting any selected preset or color override.
void theme_panel::execute(std::string_view action, editor_theme& theme)
{
    const auto selected = theme.context()->snapshot();
    const auto& color = selected->definition().colors.at(key_);
    auto candidate = theme.preferences();
    if (action == "toggle")
    {
        visible = !visible;
        editing_ = false;
    }
    else if (action == "next")
    {
        key_ = (key_ + 1) % selected->definition().colors.size();
        editing_ = false;
    }
    else if (action == "edit")
    {
        draft_.begin(ui_content::format_color(color.value));
        editing_ = true;
    }
    else if (action == "preset")
    {
        candidate.preset = candidate.preset == "dark" ? "light" : "dark";
        candidate.overrides.clear();
        theme.configure(std::move(candidate));
        editing_ = false;
    }
    else if (action == "apply")
    {
        if (!editing_)
        {
            return;
        }

        try
        {
            const auto hex = ui_content::format_color(ui_content::parse_color(draft_.value()));
            const auto found =
                std::ranges::find(candidate.overrides, color.key, &theme_override::key);
            if (found == candidate.overrides.end())
            {
                candidate.overrides.push_back({color.key, hex});
            }
            else
            {
                found->value = hex;
            }

            theme.configure(std::move(candidate));
            editing_ = false;
            status_ = "Swatch saved";
        }
        catch (const std::exception& error)
        {
            status_ = error.what();
        }
    }
    else if (action == "reset" || action == "reset-all")
    {
        if (action == "reset")
        {
            std::erase_if(candidate.overrides,
                          [&](const auto& entry)
                          {
                              return entry.key == color.key;
                          });
        }
        else
        {
            candidate.overrides.clear();
        }

        theme.configure(std::move(candidate));
        editing_ = false;
    }
}

/// @brief Cancel on blur/Escape; draft input never invokes an underlying editor shortcut.
bool theme_panel::input(std::span<const input::event> events)
{
    if (!editing_)
    {
        return false;
    }

    for (const auto& event : events)
    {
        if (event.kind == input::event_kind::focus_lost ||
            event.kind == input::event_kind::source_reset)
        {
            editing_ = false;
        }
        else if (event.kind == input::event_kind::text_input)
        {
            (void)draft_.append(static_cast<char32_t>(event.control));
        }
        else if (event.kind == input::event_kind::control_changed &&
                 event.device.kind == input::device_kind::keyboard && event.value.x != 0)
        {
            if (event.control == 8)
            {
                draft_.backspace();
            }
            else if (event.control == 27)
            {
                editing_ = false;
            }
        }
    }

    return true;
}
} // namespace atlas::editor
