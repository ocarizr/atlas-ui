/** @file @brief Graph controls and platform-independent input on the editor coordinator. */
#include "flow_panel.hpp"
#include "flow_values.hpp"
#include <algorithm>
#include <cctype>
#include <random>

namespace atlas::editor
{
namespace
{
/// @brief A fresh 128-bit authoring scope keeps reopen/delete/new operations from recycling IDs.
std::string fresh_identity_scope()
{
    std::random_device entropy;
    assets::namespace_id value;
    for (auto& byte : value.bytes)
    {
        byte = static_cast<std::uint8_t>(entropy());
    }

    value.bytes.front() |= 1;
    return assets::format_namespace(value);
}
} // namespace

/// @brief Borrow graph persistence and retain portable view/input and isolated registry state.
flow_panel::flow_panel(editor_flow::persistence& document, diagnostics::services sinks,
                       asset_repository* repository,
                       std::shared_ptr<std::pmr::memory_resource> memory,
                       const std::string& identity_scope)
    : document_{document}, tool_{document.edit()}, canvas_{tool_, sinks, memory},
      sinks_{std::move(sinks)}, repository_{repository}, registry_{{}, std::move(memory), sinks_},
      identity_scope_{identity_scope.empty()
                          ? fresh_identity_scope()
                          : assets::format_namespace(assets::parse_namespace(identity_scope))}
{
}

/// @brief Discover descriptors from registered immutable sources without invoking behavior.
void flow_panel::register_module(std::span<const scripting::source_asset> sources,
                                 scripting::callable_module entry)
{
    for (const auto& source : sources)
    {
        registry_.register_source(source);
    }

    registry_.load_callable_module(entry);
    descriptors_ = registry_.callable_descriptors();
    for (const auto& source : sources)
    {
        std::erase_if(preview_sources_,
                      [&](const auto& old)
                      {
                          return old.identity == source.identity;
                      });
        preview_sources_.push_back(source);
    }

    std::erase_if(preview_modules_,
                  [&](const auto& old)
                  {
                      return old.source == entry.source;
                  });
    preview_modules_.push_back(std::move(entry));
}

/// @brief Preserve graph changes through the independent atomic recovery contract.
void flow_panel::checkpoint()
{
    const auto& source = document_.edit().current();
    const auto& graph = source.graph;
    const bool empty = graph.screens.empty() && graph.transitions.empty() &&
                       graph.parameters.empty() && graph.regions.size() == 1 &&
                       graph.regions.front().owner.empty() && graph.regions.front().host.empty() &&
                       graph.regions.front().initial.empty() && source.layout.empty() &&
                       source.conditions.empty();
    if (empty && document_.pristine_creation())
    {
        document_.discard_recovery();
        return;
    }

    document_.checkpoint();
}

/// @brief Materialize a semantic editor command as a regular Atlas control.
void flow_panel::button(std::vector<control>& rows, std::string title, std::string command)
{
    rows.push_back(
        {{}, {0.14F, 0.21F, 0.30F, 1}, std::move(title), {control_kind::flow, std::move(command)}});
}

/// @brief Store one deferred validated field writer for the current inspector snapshot.
void flow_panel::field(std::vector<control>& rows, const std::string& key, std::string title,
                       const std::string& value, field_writer write)
{
    fields_.insert_or_assign(key, std::pair{value, std::move(write)});
    button(rows, std::move(title) + ": " + value, "field:" + key);
}

/// @brief Add an immediate bounded inspector mutation outside Atlas callbacks.
void flow_panel::operation(std::vector<control>& rows, std::string title, const std::string& key,
                           field_writer apply)
{
    fields_.insert_or_assign(key, std::pair{std::string{}, std::move(apply)});
    button(rows, std::move(title), "apply:" + key);
}

/// @brief Start with an owned text copy, preserving authored values until explicit confirmation.
void flow_panel::edit_field(std::string_view key)
{
    const auto found = fields_.find(std::string{key});
    if (found == fields_.end())
    {
        throw std::invalid_argument("Inspector changed; select the field again");
    }

    editing_ = key;
    text_ = found->second.first;
}

/// @brief Keep failed text edits visible while preserving the previous valid source revision.
void flow_panel::commit_field()
{
    const auto found = fields_.find(editing_);
    if (found == fields_.end())
    {
        throw std::invalid_argument("Inspector field is no longer available");
    }

    found->second.second(text_);
    editing_.clear();
    text_.clear();
}

/// @brief Stage bounded field text and modifier changes without entering Atlas callbacks.
bool flow_panel::key_input(std::uint32_t key, bool pressed)
{
    bool changed = false;
    if (!pressed && key == 'M')
    {
        extend_ = false;
    }

    if (!pressed && key == static_cast<unsigned>(input::key::space))
    {
        pan_ = false;
    }

    if (editing_.empty())
    {
        if (key == 'M')
        {
            extend_ = pressed;
        }

        if (key == static_cast<unsigned>(input::key::space))
        {
            pan_ = pressed;
        }
    }
    else if (pressed)
    {
        try
        {
            if (key == static_cast<unsigned>(input::key::enter))
            {
                commit_field();
            }
            else if (key == static_cast<unsigned>(input::key::escape))
            {
                editing_.clear();
            }
            else if (key == static_cast<unsigned>(input::key::backspace) ||
                     key == static_cast<unsigned>(input::key::left))
            {
                if (!text_.empty())
                {
                    text_.pop_back();
                }
            }
            else if (key == static_cast<unsigned>(input::key::up) ||
                     key == static_cast<unsigned>(input::key::down))
            {
                text_ =
                    step_flow_number(text_, key == static_cast<unsigned>(input::key::up) ? 1 : -1);
            }
            else if (((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9')) &&
                     text_.size() < 128)
            {
                text_.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(key))));
            }
            else if (key == static_cast<unsigned>(input::key::space) && text_.size() < 128)
            {
                text_ += '_';
            }
        }
        catch (const std::exception& error)
        {
            status_ = error.what();
            diagnostics::write(sinks_, diagnostics::log_level::warning, "editor.flow", status_);
        }

        changed = true;
    }

    return changed;
}

/// @brief Normalize physical coordinates once and route through persistent Atlas capture state.
bool flow_panel::input(std::span<const input::event> events, float scale)
{
    if (preview_visible_)
    {
        return false;
    }

    if (!visible)
    {
        return false;
    }

    bool changed = false;
    bridge_.clear();
    for (auto event : events)
    {
        if (event.kind == input::event_kind::focus_lost ||
            event.kind == input::event_kind::source_reset)
        {
            changed |= canvas_.process(bridge_.values(), extend_, pan_);
            bridge_.clear();
            extend_ = pan_ = false;
            editing_.clear();
        }

        if (event.device.kind == input::device_kind::keyboard &&
            event.kind == input::event_kind::control_changed)
        {
            changed |= canvas_.process(bridge_.values(), extend_, pan_);
            bridge_.clear();
            changed |= key_input(event.control, event.value.x > 0);
        }

        if (event.device.kind == input::device_kind::pointer)
        {
            event.position.x /= scale;
            event.position.y /= scale;
            if (event.kind == input::event_kind::pointer_motion)
            {
                event.value.x /= scale;
                event.value.y /= scale;
            }
        }

        if (editing_.empty() || event.device.kind != input::device_kind::pointer)
        {
            bridge_.on_input(event);
        }
    }

    changed |= canvas_.process(bridge_.values(), extend_, pan_);
    return changed;
}

/// @brief Catch user-facing validation/storage failures without retiring either document.
bool flow_panel::execute(const control_action& action, document_session& ui)
{
    if (action.kind == control_kind::flow_toggle)
    {
        editing_.clear();
        visible = !visible;
        return true;
    }

    if (action.kind != control_kind::flow)
    {
        return false;
    }

    try
    {
        const diagnostics::operation_scope scope{sinks_, {"editor.flow.command", "editor.flow"}};
        status_.clear();
        if (!action.target.starts_with("field:"))
        {
            editing_.clear();
        }

        if (action.target.starts_with("field:"))
        {
            edit_field(action.target.substr(6));
        }
        else if (action.target.starts_with("apply:"))
        {
            const auto found = fields_.find(action.target.substr(6));
            if (found == fields_.end())
            {
                throw std::invalid_argument("Inspector command is no longer available");
            }

            found->second.second({});
        }
        else if (!preview_command(action.target) && !command(action.target, ui))
        {
            throw std::invalid_argument("Unknown Flow editor command");
        }
    }
    catch (const std::exception& error)
    {
        status_ = error.what();
        diagnostics::write(sinks_, diagnostics::log_level::warning, "editor.flow", status_);
    }

    return true;
}

/// @brief Compose clipped graph paint and paginated Atlas inspector controls in logical DIPs.
void flow_panel::append(panel_plan& plan, ui::layout_engine& layout, ui::paint_engine& paint,
                        const document_session& ui)
{
    const auto bounds = plan.preview;
    if (preview_visible_)
    {
        append_preview(plan);
        return;
    }

    graph_bounds_ = {bounds.x + 4, bounds.y + 54, std::max(1.F, bounds.width - 256),
                     std::max(1.F, bounds.height - 80)};
    plan.tool = canvas_.arrange(graph_bounds_, layout, paint);
    for (const auto& item : canvas_.labels())
    {
        if (item.bounds.width > 12 && item.bounds.height > 12)
        {
            plan.controls.push_back({item.bounds, {0, 0, 0, 0}, item.text});
        }
    }

    const std::array actions{"screen", "region",  "connect", "delete", "save",  "undo",
                             "redo",   "recover", "discard", "zoom+",  "zoom-", "preview:apply"};
    const std::array titles{"Screen +",  "Region +", "Connect", "Delete",
                            "Save Flow", "Undo",     "Redo",    "Recover",
                            "Discard",   "Zoom +",   "Zoom -",  "Apply preview"};
    const auto width = std::max(1.F, (bounds.width - 8) / 6 - 4);
    for (std::size_t i = 0; i < actions.size(); ++i)
    {
        const auto toolbar_row = i / 6;
        plan.controls.push_back({{bounds.x + 4 + static_cast<float>(i % 6) * (width + 4),
                                  bounds.y + static_cast<float>(toolbar_row) * 26, width, 23},
                                 {0.14F, 0.21F, 0.30F, 1},
                                 titles[i],
                                 {control_kind::flow, actions[i]}});
    }

    fields_.clear();
    std::vector<control> rows;
    inspect(rows, ui);
    const auto count =
        std::max(std::size_t{1}, static_cast<std::size_t>(std::max(1.F, bounds.height - 116) / 25));
    const auto pages = std::max(std::size_t{1}, (rows.size() + count - 1) / count);
    page_ %= pages;
    for (std::size_t i = page_ * count; i < std::min(rows.size(), (page_ + 1) * count); ++i)
    {
        rows[i].bounds = {bounds.x + bounds.width - 244,
                          bounds.y + 54 + static_cast<float>(i % count) * 25, 240, 23};
        plan.controls.push_back(std::move(rows[i]));
    }

    plan.controls.push_back(
        {{bounds.x + bounds.width - 244, bounds.y + bounds.height - 54, 240, 23},
         {0.14F, 0.21F, 0.30F, 1},
         "Inspector page " + std::to_string(page_ + 1) + "/" + std::to_string(pages),
         {control_kind::flow, "page"}});
    plan.controls.push_back(
        {{bounds.x + 4, bounds.y + bounds.height - 26, std::max(1.F, bounds.width - 8), 23},
         {0, 0, 0, 0},
         editing_.empty()
             ? (status_.empty()
                    ? "Drag nodes | M: multiselect | Space: pan | background: pan | XML draft"
                    : status_)
             : editing_ + ": " + text_ + " | Enter: apply, Esc: cancel, Space: underscore"});
    if (preview_ && (preview_->running() || preview_->applying()))
    {
        plan.controls.back().bounds.width = std::max(1.F, bounds.width - 120);
        plan.controls.push_back(
            {{bounds.x + bounds.width - 112, bounds.y + bounds.height - 26, 108, 23},
             {0.14F, 0.21F, 0.30F, 1},
             "Resume preview",
             {control_kind::flow, "preview:show"}});
    }
}
} // namespace atlas::editor
