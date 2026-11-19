/** @file @brief Document references and previews retain immutable catalog service lifetimes. */
#include "asset_analysis.hpp"
#include "document_workspace.hpp"
#include <algorithm>

namespace atlas::editor
{
/// @brief A retained catalog snapshot provides source bytes throughout preview and history edits.
std::unique_ptr<ui_content::instance>
document_session::make_preview(const ui_content::document& source,
                               const std::shared_ptr<streaming::catalog_streamer>& mount)
{
    const diagnostics::operation_scope scope{options_.diagnostics,
                                             {"editor.preview.build", "editor"}};
    if (source.elements.empty() || source.elements.size() > 256)
    {
        throw std::invalid_argument("Editor document must contain 1 to 256 elements");
    }

    const auto& selected = mount ? mount : asset_mount_;
    const ui_content::construction effective{selected ? *selected : options_.storage,
                                             options_.factories,
                                             options_.memory,
                                             options_.diagnostics,
                                             options_.reads,
                                             options_.script_diagnostics,
                                             options_.services,
                                             options_.materials,
                                             options_.model_schemas,
                                             options_.staged,
                                             options_.navigation,
                                             options_.navigate};
    try
    {
        return ui_content::instance::build(source, effective);
    }
    catch (const std::out_of_range&)
    {
        bool missing = false;
        for (const auto ref : asset_references(source))
        {
            try
            {
                (void)effective.storage.describe(ref);
            }
            catch (const std::out_of_range&)
            {
                missing = true;
            }
        }

        if (!missing)
        {
            throw;
        }

        diagnostics::write(options_.diagnostics, diagnostics::log_level::warning, "editor.preview",
                           "Preview placeholder: referenced assets are unavailable");
        auto placeholder = new_document();
        placeholder.elements.back().paint.color = {0.7F, 0.15F, 0.2F, 1};
        return ui_content::instance::build(placeholder, options_);
    }
}

/// @brief Selection-based reference editing is transactional and participates in document history.
void document_session::assign_asset(assets::asset_ref reference,
                                    std::shared_ptr<streaming::catalog_streamer> mount)
{
    if (!mount)
    {
        throw std::invalid_argument("Asset assignment requires a preview mount");
    }

    (void)mount->describe(reference);
    auto candidate = document();
    auto selected_id = current_.selected;
    auto& node =
        *std::ranges::find(candidate.elements, current_.selected, &ui_content::element::id);
    if (reference.type == assets::asset_type::texture)
    {
        node.texture = reference;
        node.material.reset();
        node.paint.material.reset();
        node.paint.fill = true;
    }
    else if (reference.type == assets::asset_type::material)
    {
        node.material = reference;
        node.texture.reset();
        node.paint.material.reset();
        node.paint.fill = true;
    }
    else
    {
        throw std::invalid_argument(
            "Selected elements accept texture or material asset references");
    }

    auto ready = make_preview(candidate, mount);
    const auto revision = allocate_revision();
    auto old_mount = std::move(asset_mount_);
    auto old_preview = std::move(preview_);
    if (undo_.size() == 32)
    {
        undo_.erase(undo_.begin());
    }

    undo_.push_back(std::move(current_));
    current_ = {std::move(candidate), std::move(selected_id), revision};
    asset_mount_ = std::move(mount);
    preview_ = std::move(ready);
    redo_.clear();
}

/// @brief Build privately before releasing the previous preview and its retained mount.
void document_session::refresh_assets(std::shared_ptr<streaming::catalog_streamer> mount)
{
    if (!mount)
    {
        throw std::invalid_argument("Preview refresh requires a catalog mount");
    }

    auto ready = make_preview(document(), mount);
    auto old_mount = std::move(asset_mount_);
    auto old_preview = std::move(preview_);
    asset_mount_ = std::move(mount);
    preview_ = std::move(ready);
}

/// @brief Derived previews change without adding document history or clearing recovery state.
void document_workspace::refresh_assets(const std::shared_ptr<streaming::catalog_streamer>& mount)
{
    for (auto& entry : entries_)
    {
        entry.view->refresh_assets(mount);
    }
}

/// @brief History is a live consumer because undo can restore a reference.
bool document_session::references_asset(assets::asset_ref reference) const
{
    const auto uses = [&](const ui_content::document& source)
    {
        const auto refs = asset_references(source);
        return std::ranges::find(refs, reference) != refs.end();
    };
    return uses(document()) ||
           std::ranges::any_of(undo_,
                               [&](const auto& item)
                               {
                                   return uses(item.source);
                               }) ||
           std::ranges::any_of(redo_,
                               [&](const auto& item)
                               {
                                   return uses(item.source);
                               });
}

/// @brief Removing catalog assets must respect active and retained document state.
bool document_workspace::references_asset(assets::asset_ref reference) const
{
    return std::ranges::any_of(entries_,
                               [&](const auto& entry)
                               {
                                   return entry.view->references_asset(reference);
                               });
}
} // namespace atlas::editor
