/** @file @brief Immutable per-root buffers and bounded incremental layout coordination. */
#pragma once
#include "layout_validation.hpp"
#include <atlas/ui/frame_executor.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <thread>

namespace atlas::ui::detail
{
inline constexpr auto no_layout_node =
    std::numeric_limits<std::uint32_t>::max(); ///< Missing local link.

/// @brief Geometry-only input excludes visual properties and dirty acknowledgments.
struct layout_node
{
    element_handle target;                 ///< Identity is part of exact cache equality.
    std::uint32_t parent = no_layout_node; ///< Local preorder parent index.
    std::uint32_t first = no_layout_node;  ///< Local first-child index.
    std::uint32_t next = no_layout_node;   ///< Local next-sibling index.
    panel_layout panel{};                  ///< Typed container policy.
    std::uint32_t slot = no_layout_node;   ///< Sparse slot index interpreted by the parent policy.
    layout_style style;                    ///< Validated parameters copied from the tree.
    float width = 0;                       ///< Requested numeric width.
    float height = 0;                      ///< Requested numeric height.
    bool width_fixed = false;              ///< Automatic and fixed zero are distinct.
    bool height_fixed = false;             ///< Automatic and fixed zero are distinct.
    bool operator==(const layout_node&) const = default; ///< No hash-only cache decisions.
};

/// @brief Resolved immutable dependency edge, ready for future subtree scheduling.
struct resolved_boundary
{
    std::uint32_t target;     ///< Local dependent index.
    std::uint32_t source;     ///< Validated local sibling index.
    bool horizontal;          ///< Selects the axis without changing graph ownership.
    boundary_relation policy; ///< Full exact cache identity and checked numeric policy.
    bool operator==(const resolved_boundary&) const = default; ///< No hash-only reuse.
};

/// @brief One immutable independently owned preorder branch.
struct layout_source
{
    explicit layout_source(std::shared_ptr<std::pmr::memory_resource> resource)
        : memory(std::move(resource)), slots(0, memory.get()), stack_slots(0, memory.get()),
          nodes(0, memory.get()), boundaries(0, memory.get())
    {
    } ///< Retain resource before vectors.

    std::shared_ptr<std::pmr::memory_resource> memory; ///< Outlives input storage.
    std::pmr::vector<canvas_slot> slots;      ///< Allocate only for explicit canvas overrides.
    std::pmr::vector<stack_slot> stack_slots; ///< Only explicit stack overrides.
    std::pmr::vector<layout_node> nodes; ///< Parents precede descendants; child order is stable.
    std::pmr::vector<resolved_boundary> boundaries; ///< Target-preorder then horizontal/vertical.
};

/// @brief Default placement is a shared constant, not per-node allocation/storage.
inline const canvas_slot& placement(const layout_source& source, const layout_node& node)
{
    static constexpr canvas_slot default_slot{};
    return node.slot == no_layout_node ? default_slot : source.slots[node.slot];
}

/// @brief A node's parent policy determines which sparse slot pool its index addresses.
inline const stack_slot& stack_placement(const layout_source& source, const layout_node& node)
{
    static constexpr stack_slot default_slot{};
    return node.slot == no_layout_node ? default_slot : source.stack_slots[node.slot];
}

void order_stack_children(layout_source& source); ///< Sort private sibling links with stable ties.
void order_boundary_children(layout_source& source); ///< Stable topological canvas child links.

/// @brief Measure outputs remain private until the phase joins.
struct measured_branch
{
    explicit measured_branch(std::shared_ptr<const layout_source> input)
        : source(std::move(input)), desired(0, source->memory.get())
    {
        desired.resize(source->nodes.size());
    } ///< Allocate once per dirty independent branch, not once per element.

    std::shared_ptr<const layout_source> source; ///< Retain stable input and allocation policy.
    std::pmr::vector<layout_extent> desired;     ///< Natural desired size per local node.
};

/// @brief Arrange outputs retain their complete measured/source ownership chain.
struct arranged_branch
{
    explicit arranged_branch(std::shared_ptr<const measured_branch> input)
        : measured(std::move(input)), items(0, measured->source->memory.get())
    {
        items.resize(measured->desired.size());
    } ///< Separate mutable output buffer for each worker-owned root.

    std::shared_ptr<const measured_branch> measured; ///< Immutable measure dependency.
    std::pmr::vector<layout_item> items;             ///< Complete numeric output in local preorder.
};

using measure_input = std::shared_ptr<const layout_source>;    ///< Typed immutable root input.
using measure_output = std::shared_ptr<const measured_branch>; ///< Joined measure ownership.
using arrange_output = std::shared_ptr<const arranged_branch>; ///< Joined arrange/cache ownership.

/// @brief Root geometry uses one logical viewport without any native-window dependency.
struct arrange_input
{
    measure_output measured; ///< Completed measure phase.
    layout_rect viewport;    ///< Captured root slot.
};

/// @brief Global ordering metadata prepared only on the coordinator.
struct layout_forest
{
    explicit layout_forest(std::pmr::memory_resource* memory) : roots(0, memory), records(0, memory)
    {
    } ///< Counted metadata construction.

    std::pmr::vector<measure_input> roots;   ///< Independent roots in stable slot order.
    std::pmr::vector<std::uint32_t> records; ///< Slot-to-global-record mapping.
};

/// @brief Build iterative preorder branches without registry access on workers.
layout_forest prepare_layout(const element_snapshot& snapshot,
                             const std::shared_ptr<std::pmr::memory_resource>& memory);
measure_output measure_layout(measure_input source,
                              const std::stop_token& stop); ///< Bottom-up natural sizing.
/// @brief Measure one node after all its children, with the same serial reduction order.
void measure_node(measured_branch& result, std::size_t index, const std::stop_token& stop);
void measure_nodes(measured_branch& result, std::span<layout_extent> output,
                   const std::stop_token& stop); ///< Complete reverse-preorder interval kernel.
measure_output
measure_subtrees(measure_input source, const jobs::job_dispatcher& dispatcher,
                 const layout_limits& limits, const jobs::job_options& options,
                 layout_statistics& stats,
                 const diagnostics::services&
                     destinations); ///< Bounded frontier join before ancestor reduction.
arrange_output arrange_layout(const arrange_input& input,
                              const std::stop_token& stop); ///< Top-down placement.
void measure_roots(
    const phase_range<measure_input, measure_output>& range); ///< Capture-free joined kernel.
void arrange_roots(
    const phase_range<arrange_input, arrange_output>& range); ///< Capture-free joined kernel.

/// @brief Publication retains only numeric values, not mutable algorithm storage.
class layout_snapshot_data
{
  public:
    explicit layout_snapshot_data(std::shared_ptr<std::pmr::memory_resource> resource)
        : memory(std::move(resource)), items(0, memory.get())
    {
    } ///< Retain resource before output.

    std::shared_ptr<std::pmr::memory_resource> memory; ///< Outlives final values.
    std::uint64_t session = 0;                         ///< Captured tree identity.
    std::uint64_t revision = 0;                        ///< Captured geometry-input identity.
    layout_rect viewport{};                            ///< Captured logical bounds.
    std::size_t root_count = 0;          ///< Constant-time whole-result cache statistics.
    std::pmr::vector<layout_item> items; ///< Ascending global slot order.
};
} // namespace atlas::ui::detail

namespace atlas::ui
{
/// @brief Owner-only cache state; every accepted worker phase joins before mutation resumes.
class layout_engine::impl
{
  public:
    /// @brief Configure injected phases without creating a scheduler.
    impl(jobs::job_dispatcher client, layout_limits policy,
         std::shared_ptr<std::pmr::memory_resource> resource, diagnostics::services services);
    void check() const; ///< Affinity and reentrancy preflight.
    /// @brief Commit the cache only after both phases and flattening succeed.
    std::shared_ptr<const detail::layout_snapshot_data> compute(const element_snapshot& snapshot,
                                                                const jobs::job_options& options);
    /// @brief Run both joined numeric phases over changed independent roots.
    std::pmr::vector<detail::arrange_output>
    evaluate(std::span<const detail::measure_input> pending, layout_rect viewport,
             const jobs::job_options& options, bool parallel);
    const std::thread::id owner = std::this_thread::get_id(); ///< Sole cache coordinator.
    jobs::job_dispatcher dispatcher;                          ///< Shared pool client, never owned.
    layout_limits limits;                              ///< Validated bounds and scheduling policy.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Retained allocation policy.
    diagnostics::services destinations;                ///< Optional logging/profiling outputs.
    frame_executor frames; ///< Reuses the same injected dispatcher for joined phases.
    std::pmr::vector<detail::arrange_output> cache; ///< Previous roots indexed by public slot.
    std::shared_ptr<const detail::layout_snapshot_data> last; ///< Whole-input cache.
    layout_statistics stats;                                  ///< Latest attempt observations.
    std::uint64_t frame = 0; ///< Internal monotonic phase identity.
    bool busy = false;       ///< Covers callbacks, allocation and publication.
};
} // namespace atlas::ui
