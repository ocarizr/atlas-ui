/** @file @brief Owner-only file selection, catalog edits and typed document assignment. */
#include "asset_panel.hpp"
#include <algorithm>

namespace atlas::editor
{
/// @brief Look up the stable ID in the current catalog, never a dragged path or selected row.
void asset_panel::drop(assets::asset_id identity, std::string_view target,
                       document_session& document, jobs::job_system& jobs,
                       const diagnostics::services& sinks)
{
    const auto catalog = repository_.snapshot();
    const auto& records = catalog.document().records;
    const auto found = std::ranges::find_if(records,
                                            [&](const auto& row)
                                            {
                                                return row.reference.id == identity;
                                            });
    if (found == records.end())
    {
        throw std::invalid_argument("Asset drop identity is unavailable");
    }

    document.assign_asset(found->reference, mount_editor_assets(catalog, root_, jobs, sinks),
                          target);
}

/// @brief The repository validates the destination before accepting the selected source.
void asset_panel::transfer(control_kind kind)
{
    std::optional<assets::asset_ref> ref;
    if (kind != control_kind::assets_import)
    {
        ref = selected();
    }

    if (kind == control_kind::assets_refresh && ref)
    {
        const auto& records = repository_.snapshot().document().records;
        const auto found = std::ranges::find(records, *ref, &assets::asset_record::reference);
        repository_.begin({{}, found->source, ref->type, ref, false});
        return;
    }

    const auto source = choose_asset_file(root_, type_, kind == control_kind::assets_move);
    if (!source)
    {
        return;
    }

    const auto relative =
        source->lexically_relative(root_ / std::string{assets::format_type(type_)});
    const bool inside = !relative.empty() && !relative.is_absolute() && *relative.begin() != "..";
    const auto name = inside ? relative.generic_string() : source->filename().generic_string();
    asset_transfer request{
        *source, {std::string{assets::format_type(type_)}, name}, type_, ref, !inside};
    if (kind == control_kind::assets_move && ref)
    {
        if (!inside)
        {
            throw std::invalid_argument("Move destinations must be inside the asset type root");
        }

        const auto& records = repository_.snapshot().document().records;
        const auto found = std::ranges::find(records, *ref, &assets::asset_record::reference);
        request.source = root_ / found->source.root / found->source.path;
        request.copy = true;
    }

    repository_.begin(std::move(request));
}

/// @brief Return whether metadata changed, keeping document histories as deletion roots.
bool asset_panel::mutate(control_kind kind, document_workspace* documents)
{
    const auto referenced = [documents](assets::asset_ref ref)
    {
        return documents && documents->references_asset(ref);
    };
    switch (kind)
    {
        case control_kind::assets_undo:
            return repository_.undo(referenced);
        case control_kind::assets_redo:
            return repository_.redo(referenced);
        case control_kind::assets_settings:
        {
            const auto& records = repository_.snapshot().document().records;
            const auto found =
                std::ranges::find(records, selected(), &assets::asset_record::reference);
            repository_.configure(selected(), found->settings_version + 1);
            break;
        }
        case control_kind::assets_remove:
            if (documents && documents->references_asset(selected()))
            {
                throw std::invalid_argument(
                    "Asset is referenced by an open document or its history");
            }
            repository_.remove(selected());
            selected_.reset();
            break;
        default:
            return false;
    }

    return true;
}

/// @brief Commands are evaluated after event dispatch, using the currently selected stable ID.
bool asset_panel::execute(const control_action& action, document_workspace* documents,
                          jobs::job_system& jobs, const diagnostics::services& sinks)
{
    switch (action.kind)
    {
        case control_kind::assets_toggle:
            visible = !visible;
            return true;
        case control_kind::assets_type:
            type_ = static_cast<assets::asset_type>(
                (static_cast<unsigned>(type_) + 1) %
                (static_cast<unsigned>(assets::asset_type::widget) + 1));
            selected_.reset();
            page_ = 0;
            break;
        case control_kind::assets_select:
            selected_ = assets::asset_ref{assets::parse_id(action.target), type_};
            (void)selected();
            break;
        case control_kind::assets_import:
        case control_kind::assets_move:
        case control_kind::assets_relink:
        case control_kind::assets_refresh:
            transfer(action.kind);
            break;
        case control_kind::assets_cancel:
            repository_.cancel();
            break;
        case control_kind::assets_undo:
        case control_kind::assets_redo:
        case control_kind::assets_settings:
        case control_kind::assets_remove:
            if (mutate(action.kind, documents) && documents)
            {
                documents->refresh_assets(
                    mount_editor_assets(repository_.snapshot(), root_, jobs, sinks));
            }
            break;
        case control_kind::assets_next:
            ++page_;
            break;
        case control_kind::assets_assign:
            if (!documents)
            {
                throw std::logic_error("No editable document workspace");
            }
            documents->active().assign_asset(
                selected(), mount_editor_assets(repository_.snapshot(), root_, jobs, sinks));
            break;
        case control_kind::assets_open:
        {
            if (!documents || !assets::is_ui(selected().type))
            {
                throw std::invalid_argument("Open requires a UI asset and document workspace");
            }

            const auto& records = repository_.snapshot().document().records;
            const auto found =
                std::ranges::find(records, selected(), &assets::asset_record::reference);
            (void)documents->open(root_ / found->source.root / found->source.path,
                                  mount_editor_assets(repository_.snapshot(), root_, jobs, sinks));
            break;
        }
        default:
            return false;
    }

    status_ = "Ready";
    return true;
}
} // namespace atlas::editor
