/** @file @brief Bounded asset browsing, path search and reference inspection. */
#include "asset_panel.hpp"
#include <algorithm>

namespace atlas::editor
{
/// @brief Origin configuration remains independent of project scaffolding.
asset_panel::asset_panel(asset_repository& repository, std::filesystem::path root)
    : repository_{repository}, root_{std::move(root)}
{
}

/// @brief Refuse stale row actions after deletion or type changes.
assets::asset_ref asset_panel::selected() const
{
    if (!selected_)
    {
        throw std::logic_error("Select an asset first");
    }

    (void)repository_.snapshot().resolve(*selected_);
    return *selected_;
}

/// @brief Search consumes only collected key events.
bool asset_panel::keyboard(std::span<const input::event> events)
{
    bool changed = false;
    for (const auto& event : events)
    {
        if (event.kind != input::event_kind::control_changed ||
            event.device.kind != input::device_kind::keyboard || event.value.x <= 0)
        {
            continue;
        }

        if (event.control == static_cast<unsigned>(input::key::escape))
        {
            query_.clear();
            changed = true;
        }
        else if (event.control == static_cast<unsigned>(input::key::left) && !query_.empty())
        {
            query_.pop_back();
            changed = true;
        }
        else if (((event.control >= 'A' && event.control <= 'Z') ||
                  (event.control >= '0' && event.control <= '9')) &&
                 query_.size() < 64)
        {
            query_.push_back(static_cast<char>(event.control));
            changed = true;
        }
    }

    if (changed)
    {
        page_ = 0;
    }

    return changed;
}

/// @brief Materialize bounded Atlas rows and command buttons.
void asset_panel::append(panel_plan& plan)
{
    const auto bounds = plan.preview;
    plan.tool = ui::paint_snapshot{};
    const auto button = [&](float x, float y, float width, std::string title, control_kind kind,
                            std::string target = {})
    {
        plan.controls.push_back({{bounds.x + x, bounds.y + y, width, 23},
                                 {0.14F, 0.21F, 0.30F, 1},
                                 std::move(title),
                                 {kind, std::move(target)}});
    };
    const auto label = [&](float y, std::string text)
    {
        plan.controls.push_back({{bounds.x + 4, bounds.y + y, std::max(1.F, bounds.width - 8), 22},
                                 {0, 0, 0, 0},
                                 std::move(text)});
    };
    button(4, 0, 110, std::string{assets::format_type(type_)}, control_kind::assets_type);
    button(118, 0, 62, "Import", control_kind::assets_import);
    button(184, 0, 62, "Undo", control_kind::assets_undo);
    button(250, 0, 62, "Redo", control_kind::assets_redo);
    button(316, 0, 62, "Cancel", control_kind::assets_cancel);
    label(26, "Search: " + query_ + " (type; Left erases; Esc clears)");
    const auto rows = repository_.browse(type_, query_);
    constexpr std::size_t per_page = 7;
    const auto pages = std::max(std::size_t{1}, (rows.size() + per_page - 1) / per_page);
    page_ %= pages;
    for (std::size_t i = page_ * per_page; i < std::min(rows.size(), (page_ + 1) * per_page); ++i)
    {
        const auto& row = rows[i];
        button(4, 54 + static_cast<float>(i % per_page) * 25, std::max(1.F, bounds.width - 8),
               (row.missing ? "Missing " : "") + row.record.source.path,
               control_kind::assets_select, assets::format_id(row.record.reference.id));
        if (!row.missing && (row.record.reference.type == assets::asset_type::texture ||
                             row.record.reference.type == assets::asset_type::material))
        {
            plan.controls.back().source = ui::drag_source_options{
                ui::drag_payload::text("atlas.asset", assets::format_id(row.record.reference.id)),
                ui::drag_operations::copy};
        }
    }

    button(4, 232, 62, "Next", control_kind::assets_next);
    button(70, 232, 62, "Move", control_kind::assets_move);
    button(136, 232, 62, "Relink", control_kind::assets_relink);
    button(202, 232, 62, "Refresh", control_kind::assets_refresh);
    button(268, 232, 62, "Remove", control_kind::assets_remove);
    button(334, 232, 62, "Assign", control_kind::assets_assign);
    button(4, 258, 62, "Open UI", control_kind::assets_open);
    button(70, 258, 90, "Settings +", control_kind::assets_settings);
    label(284, repository_.busy() ? "Loading source..." : status_);
    if (selected_)
    {
        const auto& records = repository_.snapshot().document().records;
        const auto found = std::ranges::find(records, *selected_, &assets::asset_record::reference);
        if (found != records.end())
        {
            label(308, assets::format_id(found->reference.id));
            label(332, "Revision " + std::to_string(found->revision) + " | " + found->importer +
                           " | settings " + std::to_string(found->settings_version));
            label(356, "Dependencies " + std::to_string(found->dependencies.size()) +
                           " | References " +
                           std::to_string(repository_.dependents(*selected_).size()));
        }
    }
}

/// @brief Surface failures without replacing committed metadata.
bool asset_panel::poll(document_workspace* documents, jobs::job_system& jobs,
                       const diagnostics::services& sinks)
{
    if (!repository_.busy())
    {
        return false;
    }

    try
    {
        if (repository_.poll())
        {
            status_ = "Catalog saved";
            if (documents)
            {
                documents->refresh_assets(
                    mount_editor_assets(repository_.snapshot(), root_, jobs, sinks));
            }

            return true;
        }

        return !repository_.busy();
    }
    catch (const std::exception& error)
    {
        status_ = error.what();
        diagnostics::write(sinks, diagnostics::log_level::warning, "editor.assets", status_);
        return true;
    }
}
} // namespace atlas::editor
