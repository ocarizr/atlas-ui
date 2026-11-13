/** @file @brief Private resolved paint descriptors and joined execution storage. */
#pragma once
#include "paint_transform.hpp"
#include <atlas/ui/paint_engine.hpp>
#include <thread>
#include <vector>

namespace atlas::ui::detail
{
struct paint_links
{
    paint_links(std::size_t count, std::pmr::memory_resource* memory)
        : first(count + 1, count, memory), next(count, count, memory)
    {
    } ///< Construct potentially throwing PMR storage in place, without vector moves.

    std::pmr::vector<std::size_t> first; ///< First ordered child; final entry is the root group.
    std::pmr::vector<std::size_t> next;  ///< Stable next sibling in painter order.
}; ///< No recursive containers or live registry links.

/// @brief Verify exact session, revision, viewport and captured slot identities.
std::size_t validate_paint_geometry(const element_snapshot& input, const layout_snapshot& geometry);
///< Verify exact session, revision, viewport and captured slot identities.
void order_paint(std::span<const element_record> nodes, std::size_t slots,
                 std::pmr::memory_resource* memory,
                 paint_links& links); ///< Resolve sibling order in place.

struct paint_input
{
    element_handle target;     ///< Captured immutable identity.
    layout_rect bounds;        ///< Fitted rectangle before transform.
    layout_rect hull;          ///< Axis-aligned transformed geometry bounds.
    layout_rect clip;          ///< Effective inherited mask.
    paint_style style;         ///< Resolved alpha and local texture region.
    paint_transform transform; ///< Captured affine map for tessellation.
}; ///< One independent tessellation input; no mutable registry references.

class paint_data
{
  public:
    explicit paint_data(std::shared_ptr<std::pmr::memory_resource> resource)
        : memory(std::move(resource)), values(0, memory.get())
    {
    } ///< Retain allocator until all immutable vertices have been destroyed.

    std::shared_ptr<std::pmr::memory_resource> memory; ///< Destroy after vector storage.
    std::pmr::vector<paint_quad> values;               ///< Stable back-to-front packet.
    std::uint64_t session = 0;                         ///< Captured session.
    std::uint64_t revision = 0;                        ///< Captured geometry revision.
    layout_rect viewport;                              ///< Captured logical viewport.
}; ///< Shared only after all jobs joined successfully.

struct paint_preparation
{
    explicit paint_preparation(std::pmr::memory_resource* memory)
        : inputs(0, memory), roots(0, memory)
    {
    } ///< Scratch never outlives the calculation's allocator lease.

    std::pmr::vector<paint_input> inputs; ///< Visible candidates in stable painter order.
    std::pmr::vector<std::size_t> roots;  ///< Exclusive candidate endpoints per nonempty root.
}; ///< Dependency resolution remains on the coordinator.

struct presentation_region
{
    element_handle target; ///< Stable identity, including nonpainting elements.
    element_handle parent; ///< Captured routing ancestry.
    layout_rect bounds;    ///< Axis-aligned hull before image-background fitting.
    layout_rect clip;      ///< Same inherited rectangular clip used for painting.
    bool visible;          ///< Inherited explicit visibility; opacity does not disable interaction.
    layout_rect border;    ///< Original untransformed input border.
    paint_transform transform; ///< Shared affine map for inverse hit testing.
}; ///< Optional coordinator metadata; ordinary painting does not allocate this output.

/// @brief Resolve matching geometry, stacking, translation, visibility and clips.
void prepare_paint(const element_snapshot& input, const layout_snapshot& geometry,
                   std::pmr::memory_resource* memory, const std::stop_token& stop,
                   paint_preparation& result,
                   std::pmr::vector<presentation_region>* regions = nullptr,
                   bool emit_quads = true);
void tessellate(std::span<const paint_input> inputs, std::span<paint_quad> output,
                const std::stop_token& stop); ///< Same numeric kernel for reference and workers.
void execute_paint(const paint_preparation& prepared, paint_data& output,
                   const jobs::job_dispatcher& dispatcher, paint_limits limits,
                   const jobs::job_options& options, paint_statistics& stats,
                   const diagnostics::services& destinations); ///< Join all borrowed work.
void check_paint_cancel(const std::stop_token& stop); ///< Common cooperative failure boundary.
} // namespace atlas::ui::detail
