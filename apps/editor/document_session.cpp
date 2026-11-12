/** @file @brief Transactional editor commands and bounded history over authored UI values. */
#include "document_session.hpp"
#include <algorithm>
#include <limits>
#include <set>

namespace atlas::editor
{
namespace
{
constexpr std::size_t max_history = 32;   ///< Retained undo or redo revisions.
constexpr std::size_t max_elements = 256; ///< Bootstrap hierarchy capacity.

/// @brief Compare stable IDs without retaining iterators across vector mutations.
auto find_element(const ui_content::document& source, std::string_view id)
{
    return std::ranges::find(source.elements, id, &ui_content::element::id);
}

/// @brief Retain the newest history without allocating after a candidate is published.
template <class Revision> void retain(std::vector<Revision>& history, Revision previous)
{
    if (history.size() == max_history)
    {
        history.erase(history.begin());
    }

    history.push_back(std::move(previous));
}

/// @brief Remove a complete preorder subtree and retire references to removed siblings.
void remove_subtree(ui_content::document& source, std::string_view id)
{
    std::set<std::string, std::less<>> removed{std::string{id}};
    for (const auto& node : source.elements)
    {
        if (removed.contains(node.parent))
        {
            removed.insert(node.id);
        }
    }

    for (auto& node : source.elements)
    {
        if (node.horizontal && removed.contains(node.horizontal->source))
        {
            node.horizontal.reset();
        }

        if (node.vertical && removed.contains(node.vertical->source))
        {
            node.vertical.reset();
        }
    }

    std::erase_if(source.elements,
                  [&](const auto& node)
                  {
                      return removed.contains(node.id);
                  });
}

/// @brief Give a generic leaf visible content and predictable initial geometry.
ui_content::element make_leaf(std::string id, std::string parent)
{
    ui_content::element leaf;
    leaf.id = std::move(id);
    leaf.parent = std::move(parent);
    leaf.panel = ui::leaf_layout{};
    leaf.requested.width = 120;
    leaf.requested.width_fixed = true;
    leaf.requested.height = 42;
    leaf.requested.height_fixed = true;
    leaf.paint.fill = true;
    leaf.paint.color = {0.20f, 0.55f, 0.81f, 1};
    return leaf;
}
} // namespace

/// @brief Create a generic stack that can be edited immediately.
ui_content::document new_document()
{
    ui_content::element root;
    root.id = "root";
    root.panel = ui::vertical_layout{12};
    root.layout.padding = {16, 16, 16, 16};
    root.paint.clip = true;
    return {{}, {root, make_leaf("element_0", root.id)}};
}

/// @brief Construct the initial preview and reserve all history slots.
document_session::document_session(ui_content::document source,
                                   const ui_content::construction& options)
    : options_{options}
{
    undo_.reserve(max_history);
    redo_.reserve(max_history);
    replace(std::move(source), false);
}

/// @brief Borrow immutable authoring values.
const ui_content::document& document_session::document() const noexcept
{
    return current_.source;
}

/// @brief Selection uses authored identity, independent of runtime handles.
std::string_view document_session::selected() const noexcept
{
    return current_.selected;
}

/// @brief Borrow a separate runtime tree to calculate presentation.
ui::element_tree& document_session::preview()
{
    return preview_->tree();
}

/// @brief A restored saved revision clears dirty state.
bool document_session::dirty() const noexcept
{
    return current_.id != saved_;
}

/// @brief Query whether an earlier revision is retained.
bool document_session::can_undo() const noexcept
{
    return !undo_.empty();
}

/// @brief Query whether a revision can be reapplied.
bool document_session::can_redo() const noexcept
{
    return !redo_.empty();
}

/// @brief Resolve selection by stable ID without rebuilding or creating a history entry.
void document_session::select(std::string_view id)
{
    if (find_element(document(), id) == document().elements.end())
    {
        throw std::invalid_argument("Unknown authored element selection");
    }

    current_.selected = id;
}

/// @brief Rebuild the preview before publishing either authored or runtime state.
std::unique_ptr<ui_content::instance>
document_session::make_preview(const ui_content::document& source)
{
    const diagnostics::operation_scope scope{options_.diagnostics,
                                             {"editor.preview.build", "editor"}};
    if (source.elements.empty() || source.elements.size() > max_elements)
    {
        throw std::invalid_argument("Editor document must contain 1 to 256 elements");
    }

    return ui_content::instance::build(source, options_);
}

/// @brief Give history and save state stable identities without retaining an extra snapshot.
std::uint64_t document_session::allocate_revision()
{
    if (next_revision_ == std::numeric_limits<std::uint64_t>::max())
    {
        throw std::overflow_error("Editor revision IDs exhausted");
    }

    return next_revision_++;
}

/// @brief Commit one edit and discard redo only after the candidate succeeds.
bool document_session::apply(edit_kind command)
{
    auto candidate = document();
    auto selected = current_.selected;
    const auto index =
        static_cast<std::size_t>(find_element(candidate, selected) - candidate.elements.begin());
    auto& element = candidate.elements.at(index);
    switch (command)
    {
        case edit_kind::add_child:
            if (std::holds_alternative<ui::leaf_layout>(element.panel) ||
                candidate.elements.size() == max_elements)
            {
                return false;
            }

            do
            {
                if (next_id_ == std::numeric_limits<std::uint64_t>::max())
                {
                    throw std::overflow_error("Editor element IDs exhausted");
                }

                selected = "element_" + std::to_string(next_id_++);
            } while (find_element(candidate, selected) != candidate.elements.end());
            candidate.elements.push_back(make_leaf(selected, element.id));
            break;
        case edit_kind::remove:
            if (element.parent.empty())
            {
                return false;
            }

            selected = element.parent;
            remove_subtree(candidate, current_.selected);
            break;
        case edit_kind::narrower:
        case edit_kind::wider:
            element.requested.width =
                std::clamp((element.requested.width_fixed ? element.requested.width : 120.f) +
                               (command == edit_kind::wider ? 16.f : -16.f),
                           8.f, 2048.f);
            element.requested.width_fixed = true;
            break;
        case edit_kind::toggle_visible:
            element.requested.visible = !element.requested.visible;
            break;
        default:
            throw std::invalid_argument("Unknown editor command");
    }

    auto ready = make_preview(candidate);
    const auto id = allocate_revision();
    retain(undo_, std::move(current_));
    current_ = {std::move(candidate), std::move(selected), id};
    preview_ = std::move(ready);
    redo_.clear();
    return true;
}

/// @brief Validate a retained preview before moving history between stacks.
bool document_session::undo()
{
    if (undo_.empty())
    {
        return false;
    }

    auto ready = make_preview(undo_.back().source);
    retain(redo_, std::move(current_));
    current_ = std::move(undo_.back());
    undo_.pop_back();
    preview_ = std::move(ready);
    return true;
}

/// @brief Rebuild the exact authored revision using the current composition services.
bool document_session::redo()
{
    if (redo_.empty())
    {
        return false;
    }

    auto ready = make_preview(redo_.back().source);
    retain(undo_, std::move(current_));
    current_ = std::move(redo_.back());
    redo_.pop_back();
    preview_ = std::move(ready);
    return true;
}

/// @brief Reopening and new-document operations reset history after validated publication.
void document_session::replace(ui_content::document source, bool saved)
{
    auto ready = make_preview(source);
    auto root = source.elements.front().id;
    const auto id = allocate_revision();
    current_ = {std::move(source), std::move(root), id};
    preview_ = std::move(ready);
    undo_.clear();
    redo_.clear();
    saved_ = saved ? id : 0;
}

/// @brief Mark only the last successfully saved revision.
void document_session::mark_saved() noexcept
{
    saved_ = current_.id;
}
} // namespace atlas::editor
