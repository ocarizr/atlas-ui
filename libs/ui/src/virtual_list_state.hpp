/** @file @brief Shared private staging, resource leases and logical navigation state. */
#pragma once
#include "virtual_input_edit.hpp"
#include "virtual_tree_edit.hpp"
#include <atlas/ui/virtual_list.hpp>

namespace atlas::ui::detail
{
/// @brief Reentry cleanup is unconditional, including factory and diagnostic exceptions.
struct virtual_guard
{
    /// @brief Enter a checked owner operation.
    explicit virtual_guard(bool& value) : active(value)
    {
        active = true;
    }

    /// @brief Restore the guard during exception unwinding too.
    ~virtual_guard()
    {
        active = false;
    }

    bool& active; ///< Borrowed owner guard.
};

/// @brief Shared only with owner-affine handlers, never a worker callback target.
struct virtual_navigation
{
    std::shared_ptr<const item_source> source; ///< Published immutable source.
    item_key focused = 0;                      ///< Retained logical focus.
    std::optional<item_key> pending;           ///< Realize/reveal before subsequent interaction.
    scroll_navigation_actions actions; ///< Same device-independent vocabulary as retained scrolls.
    bool active = true;                ///< Destruction makes retained handlers inert.
    bool restoring = false;            ///< Internal proxy transitions preserve the logical target.
};

/// @brief Retained by bindings through disable/end, not just by the realization table.
struct row_lease
{
    /// @brief Retain the PMR policy through delayed handler cleanup.
    explicit row_lease(std::shared_ptr<std::pmr::memory_resource> resource)
        : memory(std::move(resource)), resources(0, memory.get()), bindings(0, memory.get())
    {
    }

    std::shared_ptr<std::pmr::memory_resource>
        memory; ///< Retired handlers can outlive the controller.
    std::pmr::vector<std::shared_ptr<void>> resources; ///< Subscriptions, buffers and async owners.
    std::pmr::vector<element_handle> bindings;         ///< Published identities retired as a unit.
};

/// @brief A single staged row, bounded independently from logical source size.
class row_builder
{
  public:
    /// @brief Borrow one private draft and its bounded candidate resource ownership.
    row_builder(tree_edit& transaction, input_edit& input, realized_row row, element_handle root,
                std::shared_ptr<row_lease> lease, std::shared_ptr<virtual_navigation> navigation,
                virtual_list_limits limits);
    void
    build(const virtual_list::factory& create); ///< Invoke serial factory and finish root binding.
    void bind(element_handle target, std::shared_ptr<input_handler> handler,
              interaction_options options); ///< Validate subtree membership before admission.
    tree_edit& transaction;                 ///< Draft identity translation.
    input_edit& input;                      ///< Unpublished complete handler table.
    realized_row value;                     ///< Future published metadata.
    element_handle root;                    ///< Draft subtree root.
    std::shared_ptr<row_lease> lease;       ///< Keep resources through cleanup callbacks.
    std::shared_ptr<virtual_navigation> navigation; ///< Logical focus, independent of row lifetime.
    virtual_list_limits limits;                     ///< Fixed per-row bounds.
    bool root_bound = false; ///< Install default focus root when not supplied by the factory.
};

/// @brief Wrap user handlers with generation-independent logical focus bookkeeping.
std::shared_ptr<input_handler> row_handler(std::shared_ptr<input_handler> inner,
                                           std::shared_ptr<virtual_navigation> navigation,
                                           item_key key, std::shared_ptr<row_lease> lease);
/// @brief Capture logical navigation before built-in retained-row navigation can target overscan.
std::shared_ptr<input_handler> navigation_handler(std::shared_ptr<virtual_navigation> navigation);
} // namespace atlas::ui::detail

namespace atlas::ui
{
/// @brief Admitted logical viewport parameters, independent of screen-space point coordinates.
struct virtual_window
{
    float offset = 0; ///< Clamped logical scroll position.
    float height = 0; ///< Inside-padding viewport height.
};

/// @brief Owner-only realization coordinator with shared worker execution for metadata batches.
class virtual_list::impl
{
  public:
    /// @brief Compose borrowed owner state and reserve bounded row bookkeeping.
    impl(element_tree& tree, input_session& input, element_handle viewport,
         jobs::job_dispatcher dispatcher, factory create, virtual_list_limits limits,
         std::shared_ptr<std::pmr::memory_resource> memory, diagnostics::services destinations);
    void check() const; ///< Coordinator affinity and reentry guard.
    float offset(const item_source& source, float height) const; ///< Anchor/reveal/clamp policy.
    /// @brief Join numeric preparation before atomic tree/binding publication.
    void publish(std::shared_ptr<const item_source> source, float height,
                 const jobs::job_options& options);
    /// @brief Commit fully described rows after every accepted metadata job has joined.
    void commit_rows(std::shared_ptr<const item_source> source, virtual_window view,
                     std::pmr::vector<realized_row>& prepared, const jobs::job_options& options);
    /// @brief Build only the bounded candidate range and return its unpublished content root.
    element_handle stage(detail::tree_edit& edit, detail::input_edit& input_edit,
                         std::span<realized_row> plan, float extent,
                         std::pmr::vector<std::shared_ptr<detail::row_lease>>& leases);
    element_tree& tree;         ///< Shared host tree.
    input_session& input;       ///< Shared host serial input session.
    element_handle viewport;    ///< Vertical scroll domain.
    element_handle content{};   ///< Owned canvas, never one node per logical source row.
    factory create;             ///< Serial row construction.
    virtual_list_limits limits; ///< Explicit admission bounds.
    std::shared_ptr<std::pmr::memory_resource> memory; ///< Outlives all PMR scratch.
    diagnostics::services destinations;                ///< Optional logs and profiling.
    frame_executor work;                 ///< Injected dispatcher, threshold and coarse grain.
    std::pmr::vector<realized_row> rows; ///< Realized rows in logical order.
    std::pmr::vector<std::shared_ptr<detail::row_lease>> leases; ///< Same order as rows.
    std::shared_ptr<detail::virtual_navigation> navigation; ///< Published logical focus and source.
    virtual_list_statistics stats; ///< Successful publication observations.
    std::uint64_t serial = 0;      ///< Never reuse binding tickets after failure.
    std::uint64_t frame = 0;       ///< Increasing private metadata phase identity.
    float previous_offset = 0;     ///< Published logical anchor position.
    float previous_height = 0;     ///< Distinguish resize from manual scroll.
    const std::thread::id owner = std::this_thread::get_id(); ///< One mutable coordinator.
    bool busy = false; ///< Reject factory/diagnostic reentry.
};
} // namespace atlas::ui
