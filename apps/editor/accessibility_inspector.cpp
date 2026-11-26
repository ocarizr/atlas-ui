/** @file @brief Dedicated semantic inspector controls and single-revision Apply. */
#include "accessibility_inspector.hpp"
#include <algorithm>
#include <charconv>

namespace atlas::editor
{
namespace
{
/// @brief Scalar fields share the serialized accessibility vocabulary.
constexpr std::array fields{"name",    "description",   "name-key",      "description-key",
                            "value",   "invoke-action", "select-action", "number",
                            "minimum", "maximum"};
constexpr std::array roles{
    "group", "text",      "image",    "button", "tab-group",       "tab",
    "list",  "list-item", "checkbox", "slider", "scroll-container"}; ///< Role labels.
} // namespace

/// @brief Retire stale drafts when the selected asset or its revision changes.
void accessibility_inspector::synchronize(const document_session& session)
{
    if (element_ == session.selected() && revision_ == session.revision_id())
    {
        return;
    }

    const auto selected = std::ranges::find(session.document().elements, session.selected(),
                                            &ui_content::element::id);
    element_ = session.selected();
    revision_ = session.revision_id();
    draft_ = selected->accessible.value_or(ui::accessible_element{.name = selected->caption});
    mode_ = selected->decorative ? 2U : selected->accessible ? 1U : 0U;
    field_.clear();
    status_.clear();
}

/// @brief Present a bounded field page using ordinary accessible Atlas controls.
void accessibility_inspector::append(panel_plan& plan, const document_session& session)
{
    synchronize(session);
    const auto area = plan.inspector;
    const auto x = area.x + 8;
    const auto width = area.width - 16;
    const auto add = [&](float y, std::string label, std::string action)
    {
        plan.controls.push_back({{x, y, width, 24},
                                 {0.14f, 0.21f, 0.30f, 1},
                                 std::move(label),
                                 {control_kind::accessibility, std::move(action)}});
    };
    if (!visible)
    {
        add(area.y + area.height - 30, "Accessibility...", "toggle");
        return;
    }

    std::erase_if(plan.controls,
                  [&](const auto& item)
                  {
                      return item.bounds.x >= area.x && item.bounds.x < area.x + area.width &&
                             item.bounds.y >= area.y && item.bounds.y < area.y + area.height;
                  });
    add(area.y + 4, "Accessibility | Back", "toggle");
    const std::array modes{"Inferred", "Explicit", "Decorative"};
    add(area.y + 32, std::string{"Mode: "} + modes[mode_], "mode");
    const std::size_t rows = static_cast<std::size_t>(std::max(1.f, (area.height - 180) / 28));
    const auto pages = (fields.size() + rows - 1) / rows;
    page_ %= pages;
    if (mode_ == 1)
    {
        add(area.y + 60, std::string{"Role: "} + roles[static_cast<std::size_t>(draft_.role)],
            "role");
        for (std::size_t i = page_ * rows; i < std::min(fields.size(), (page_ + 1) * rows); ++i)
        {
            const auto field = std::string{fields[i]};
            add(area.y + 88 + static_cast<float>(i % rows) * 28,
                field + ": " + (field_ == field ? std::string{text_.value()} + " |" : value(field)),
                "field:" + field);
        }
    }

    const auto bottom = area.y + area.height;
    add(bottom - 114, "Selected: " + std::string{draft_.selected ? "yes" : "no"}, "selected");
    add(bottom - 86, "Checked: " + std::string{draft_.checked ? "yes" : "no"}, "checked");
    add(bottom - 58, field_.empty() ? "Apply | " + status_ : "Enter: confirm / Esc: cancel",
        "apply");
    add(bottom - 30,
        "Fields " + std::to_string(page_ + 1) + "/" + std::to_string(pages) + " | Next", "next");
}

/// @brief Stage inspector operations and apply semantics through one source transaction.
bool accessibility_inspector::execute(std::string_view command, document_session& session)
{
    synchronize(session);
    if (command == "toggle")
    {
        visible = !visible;
        field_.clear();
    }
    else if (command == "mode")
    {
        mode_ = (mode_ + 1) % 3;
        field_.clear();
    }
    else if (command == "role")
    {
        draft_.role = static_cast<accessibility::role>((static_cast<unsigned>(draft_.role) + 1) %
                                                       roles.size());
    }
    else if (command == "selected")
    {
        draft_.selected = !draft_.selected;
    }
    else if (command == "checked")
    {
        draft_.checked = !draft_.checked;
    }
    else if (command == "next")
    {
        ++page_;
        field_.clear();
    }
    else if (command.starts_with("field:"))
    {
        field_ = command.substr(6);
        text_.begin(value(field_));
    }
    else if (command == "apply")
    {
        if (!field_.empty())
        {
            throw std::logic_error("Confirm or cancel the active field first");
        }

        (void)session.set_accessibility(mode_ == 1 ? std::optional{draft_} : std::nullopt,
                                        mode_ == 2);
        revision_ = session.revision_id();
        status_ = "Applied";
    }
    else
    {
        throw std::invalid_argument("Unknown accessibility inspector command");
    }

    return true;
}

/// @brief Expose exclusive keyboard ownership while a text draft is active.
bool accessibility_inspector::editing() const noexcept
{
    return !field_.empty();
}

/// @brief Collect committed text without dispatching navigation or mutating source assets.
bool accessibility_inspector::input(std::span<const input::event> events, document_session& session)
{
    synchronize(session);
    if (field_.empty())
    {
        return false;
    }

    for (const auto& event : events)
    {
        if (event.kind == input::event_kind::focus_lost ||
            event.kind == input::event_kind::source_reset)
        {
            field_.clear();
            break;
        }

        if (event.kind == input::event_kind::text_input)
        {
            (void)text_.append(event.control);
        }

        if (event.kind != input::event_kind::control_changed ||
            event.device.kind != input::device_kind::keyboard || event.value.x == 0)
        {
            continue;
        }

        if (event.control == static_cast<unsigned>(input::key::backspace))
        {
            text_.backspace();
        }
        else if (event.control == static_cast<unsigned>(input::key::escape))
        {
            field_.clear();
            break;
        }
        else if (event.control == static_cast<unsigned>(input::key::enter))
        {
            try
            {
                assign({field_, text_.value()});
                field_.clear();
                status_.clear();
            }
            catch (const std::exception& error)
            {
                status_ = error.what();
            }

            break;
        }
    }

    return true;
}
} // namespace atlas::editor
