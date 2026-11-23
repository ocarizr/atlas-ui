/**
 * @file
 * @brief Bounded transactional vertical list realization over the shared scroll and input
 * contracts.
 */
#pragma once
#include <atlas/ui/frame_executor.hpp>
#include <atlas/ui/input_session.hpp>
#include <atlas/ui/virtual_source.hpp>
#include <functional>

namespace atlas::ui
{
/// @brief Validate late completions on the owner before applying any row-specific data.
struct row_ticket
{
    item_key key = 0;             ///< Stable logical identity.
    std::uint64_t generation = 0; ///< Burned on every attempted binding, including failed attempts.
    element_handle root{}; ///< Physical identity, never sufficient alone for async validation.
    bool operator==(const row_ticket&) const = default; ///< Compare all binding identity fields.
};

/// @brief Immutable realized range observation; valid only until the next successful update.
struct realized_row
{
    std::size_t index = 0; ///< Current logical order, independent of slot order.
    logical_item item{};   ///< Exact source geometry and binding revision.
    row_ticket ticket{};   ///< Current logical-to-physical association.
};

/// @brief Explicit admission and bounded worker grain; no job is created per logical item.
struct virtual_list_limits
{
    std::size_t max_realized = 128; ///< Visible plus overscan rows; overflow rejects the update.
    std::size_t overscan = 2;       ///< Maximum extra rows on each edge.
    std::size_t max_nodes_per_row = 16;    ///< Factory subtree admission, including the row root.
    std::size_t max_bindings_per_row = 8;  ///< Includes the automatic focusable row root.
    std::size_t min_parallel_items = 4096; ///< Exact metadata normally runs inline.
    std::size_t grain = 128; ///< Coarse metadata ranges when explicitly tuned above the crossover.
    std::uint32_t max_chunks = 16; ///< Shared dispatcher admission bound.
};

namespace detail
{
class row_builder; ///< Private bounded staging helper.
} // namespace detail

/// @brief Owner-only construction of a fresh row subtree; retain resources until retirement.
class row_context
{
  public:
    element_tree& tree() const noexcept;      ///< Private draft, never the live UI session.
    element_handle root() const noexcept;     ///< Already sized and positioned by the list.
    const realized_row& row() const noexcept; ///< Logical metadata plus future publication ticket.
    void bind(element_handle target, std::shared_ptr<input_handler> handler,
              interaction_options options = {}); ///< Only this row or its descendants.
    void
    retain(std::shared_ptr<void> resource); ///< RAII subscriptions/assets, released after cleanup.
  private:
    friend class detail::row_builder; ///< Only the bounded staging owner constructs this facade.
    explicit row_context(detail::row_builder& state); ///< Borrow only for the factory invocation.
    detail::row_builder& state_; ///< Never retain this context or its draft-tree reference.
};

/// @brief Last successful publication accounting; source storage and user assets are separate.
struct virtual_list_statistics
{
    std::size_t logical = 0;  ///< Source count; can greatly exceed tree capacity.
    std::size_t realized = 0; ///< Published row roots including overscan.
    std::size_t created = 0;  ///< New bindings in the last publication.
    std::size_t reused = 0;   ///< Unchanged key/revision bindings retained without factory calls.
    std::size_t admitted_jobs = 0;  ///< Joined metadata work, excluding later host layout/paint.
    std::size_t metadata_bytes = 0; ///< Reserved list row bookkeeping; excludes tree/input/source.
};

/// @brief One vertical scroll owner, one logical source, bounded rows and no private worker pool.
class virtual_list
{
  public:
    using factory =
        std::function<void(row_context&)>; ///< Serial fresh binding; heavy work uses jobs.
    virtual_list(element_tree& tree, input_session& input, element_handle viewport,
                 jobs::job_dispatcher dispatcher, factory create, virtual_list_limits limits = {},
                 std::shared_ptr<std::pmr::memory_resource> memory = {},
                 diagnostics::services destinations = {}); ///< Borrow idle owner objects.
    ~virtual_list(); ///< Call detach before destruction to remove owned UI; no implicit callbacks.
    virtual_list(const virtual_list&) = delete;            ///< Exactly one realization owner.
    virtual_list& operator=(const virtual_list&) = delete; ///< No shared mutable controller.
    /// @brief Between frames, stage source/range/rows, join metadata work, then publish atomically.
    void update(std::shared_ptr<const item_source> source, float viewport_height,
                const jobs::job_options& options = {});
    void request_focus(item_key key); ///< Queue logical reveal, including an unrealized item.
    void restore_focus(); ///< After input.begin_frame, before process; focus the realized target.
    item_key focused_item() const; ///< Logical focus survives manual scroll and physical recycling.
    void configure_navigation(scroll_navigation_actions actions); ///< Optional semantic action IDs.
    bool accepts(row_ticket ticket) const;      ///< Owner-only stale completion validation.
    std::span<const realized_row> rows() const; ///< Logical-order realized records, borrowed view.
    virtual_list_statistics statistics() const; ///< Successful publication observations.
    void detach(); ///< Between frames, remove owned content and retire all owned input bindings.
  private:
    class impl; ///< Private bounded ownership, logical focus and staged publication.
    std::unique_ptr<impl> impl_; ///< Borrowed tree/input must outlive this facade.
};
} // namespace atlas::ui
