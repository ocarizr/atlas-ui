/** @file @brief Private bounded structural bookkeeping and EnTT-owned component state. */
#pragma once
#include <atlas/ui/element_tree.hpp>
#include <entt/entity/registry.hpp>
#include <limits>
#include <thread>
#include <vector>

namespace atlas::ui
{
/// @brief No native SDK or pointer identity crosses the tree facade.
class element_tree::impl
{
  public:
    static constexpr auto none = std::numeric_limits<std::uint32_t>::max(); ///< Missing link.

    /// @brief Intrusive links avoid per-element child-vector allocations.
    struct node
    {
        requested_properties requested; ///< Committed local properties.
        invalidation dirty;             ///< Accumulated conservative work.
        std::uint32_t parent = none;    ///< Ancestor slot or none.
        std::uint32_t first = none;     ///< First child in insertion order.
        std::uint32_t last = none;      ///< Last child, allowing constant-time append.
        std::uint32_t previous = none;  ///< Previous child in the parent's list.
        std::uint32_t next = none;      ///< Next child in the parent's list.
    };

    /// @brief Public generation is independent of EnTT's private entity encoding.
    struct slot
    {
        entt::entity entity = entt::null; ///< Null when the public slot is unoccupied.
        std::uint32_t generation = 1;     ///< Never wraps; exhausted slots are retired.
    };

    /// @brief Keep diagnostic callbacks from reentering mutable state, including on failure.
    class guard
    {
      public:
        explicit guard(bool& active) : active_(active)
        {
            active_ = true;
        } ///< Enter checked scope.

        ~guard()
        {
            active_ = false;
        } ///< Restore on every exit, including exception unwinding.

        guard(const guard&) = delete;            ///< One scope owns the reset.
        guard& operator=(const guard&) = delete; ///< Never duplicate scope ownership.
      private:
        bool& active_; ///< Borrowed tree-local flag.
    };

    impl(std::uint64_t id, tree_limits limits, std::shared_ptr<std::pmr::memory_resource> resource,
         diagnostics::services services); ///< Allocate fixed slot/free/traversal capacity.
    void check() const; ///< Affinity is checked before any mutable-state observation.
    bool live(element_handle handle) const noexcept;    ///< Called only after the owner guard.
    std::uint32_t require(element_handle handle) const; ///< Reject stale or cross-session targets.
    element_handle handle(std::uint32_t index) const noexcept; ///< Map a link to public identity.
    node& at(std::uint32_t index);                             ///< Access live private state only.
    const node& at(std::uint32_t index) const;                 ///< Owner-only const state access.
    void detach(std::uint32_t index); ///< Remove a live node from its parent's list.
    void attach(std::uint32_t index, std::uint32_t parent); ///< Append after validation succeeds.
    void ancestors(std::uint32_t index); ///< Mark layout/paint on the complete ancestor chain.
    void subtree(std::uint32_t index, bool layout); ///< Mark bounded descendants without recursion.
    const std::uint64_t session; ///< Unique nonzero process-local session identity.
    const std::thread::id owner = std::this_thread::get_id(); ///< Sole mutable-state owner.
    std::shared_ptr<std::pmr::memory_resource> memory;        ///< Outlives all PMR containers.
    diagnostics::services destinations;                       ///< Optional logging and CPU capture.
    entt::registry registry;      ///< Private component/entity ownership; uses EnTT's allocator.
    std::pmr::vector<slot> slots; ///< Fixed public identity space.
    std::pmr::vector<std::uint32_t> free;     ///< Reserved reusable slot stack.
    std::pmr::vector<std::uint32_t> work;     ///< Reserved iterative traversal scratch.
    std::size_t count = 0;                    ///< Live node count.
    std::uint64_t frame = 0;                  ///< Last successfully applied frame.
    std::uint32_t ordinal = 0;                ///< Last successfully applied batch in that frame.
    bool busy = false;                        ///< Guard shared with the owned command producer.
    std::unique_ptr<command_buffer> commands; ///< Destroyed before its borrowed busy flag.
};
} // namespace atlas::ui
