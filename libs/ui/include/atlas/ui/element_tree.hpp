/**
 * @file
 * @brief Session-owned element hierarchy and transactional requested-property state.
 */
#pragma once
#include <atlas/ui/element_snapshot.hpp>
#include <atlas/ui/layout_snapshot.hpp>

namespace atlas::ui
{
/// @brief Fixed live-element capacity and the single producer's per-frame command budgets.
struct tree_limits
{
    std::uint32_t max_elements = 4096; ///< Supported range is 1..65536; includes every root.
    command_limits commands{};         ///< Independent raw-write, batch and warning budgets.
};

/// @brief Owner-affine EnTT-backed state; workers consume immutable snapshots, not this tree.
class element_tree
{
  public:
    explicit element_tree(tree_limits limits = {},
                          std::shared_ptr<std::pmr::memory_resource> memory = {},
                          diagnostics::services destinations = {}); ///< Reserve structural scratch.
    ~element_tree(); ///< Destroy on the creating thread, after stopping all borrowers.
    element_tree(const element_tree&) = delete;            ///< A mutable session has one owner.
    element_tree& operator=(const element_tree&) = delete; ///< Sessions cannot be aliased by copy.
    element_handle create(element_handle parent = {}); ///< Append a child or an independent root.
    void remove(element_handle target); ///< Remove the complete subtree; invalidate generations.
    void reparent(element_handle target, element_handle parent = {}); ///< Reject cycles atomically.
    bool contains(element_handle target) const; ///< Foreign/stale/invalid handles return false.
    std::size_t size() const;                   ///< Live elements across all roots.
    element_handle parent(element_handle target) const;       ///< Empty handle for a root.
    element_handle first_child(element_handle target) const;  ///< Empty when no children exist.
    element_handle next_sibling(element_handle target) const; ///< Stable insertion-order traversal.
    requested_properties requested(element_handle target) const; ///< Pending writes are invisible.
    invalidation dirty(element_handle target) const; ///< Read local accumulated work flags.
    void acknowledge(element_handle target);         ///< Clear only this live element's work flags.
    command_buffer& commands(); ///< Sole session-bound stream; reference cannot outlive the tree.
    void apply(const command_batch& batch); ///< Ordered, all-or-nothing scalar state publication.
    element_snapshot
    snapshot() const; ///< Copy committed values and links; never clears dirty flags.
    void set_layout(element_handle target,
                    const layout_style& style);       ///< Validate before changing inputs.
    layout_style layout(element_handle target) const; ///< Committed parameters by value.
    void set_panel(element_handle target,
                   const panel_layout& panel); ///< Validate policy and child slots atomically.
    panel_layout panel(element_handle target) const; ///< Typed container policy by value.
    void set_canvas_slot(element_handle target,
                         const canvas_slot& slot); ///< Require a canvas parent.
    void reset_canvas_slot(element_handle target); ///< Restore implicit default placement.
    std::optional<canvas_slot>
    canvas_slot_for(element_handle target) const; ///< Empty outside a canvas relationship.
    void set_visual(element_handle target,
                    visual_style visual);             ///< Paint-only general transform origin.
    visual_style visual(element_handle target) const; ///< Return committed metadata by value.
    void set_viewport(layout_rect viewport); ///< Changed bounds invalidate all computed geometry.
    void publish_layout(const layout_snapshot& result); ///< Atomically reject old/foreign inputs.
    std::optional<layout_geometry>
    computed(element_handle target) const; ///< Empty until current geometry is published.
  private:
    class impl; ///< Registry, hierarchy links, generation slots and diagnostic guards.
    std::unique_ptr<impl> impl_; ///< Keeps private EnTT/native-free implementation out of headers.
};
} // namespace atlas::ui
