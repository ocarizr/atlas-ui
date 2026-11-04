/**
 * @file
 * @brief Immutable ordered rectangle packets and bounded optional tessellation jobs.
 */
#pragma once
#include <atlas/ui/layout_engine.hpp>

namespace atlas::ui
{
struct paint_vertex
{
    layout_point position;      ///< Logical viewport position, including inherited translation.
    layout_point uv;            ///< Normalized texture coordinates.
    std::array<float, 4> color; ///< Straight-alpha color including inherited opacity.
    bool operator==(const paint_vertex&) const = default; ///< Reference numeric comparison.
}; ///< Fixed value-only vertex independent of the renderer ABI.

struct paint_quad
{
    element_handle target;     ///< Source identity for diagnostics and future hit testing.
    layout_rect bounds;        ///< Translated border rectangle before clipping.
    layout_rect clip;          ///< Intersected viewport/ancestor clip in logical coordinates.
    std::uint64_t texture = 0; ///< Consumer-resolved resident key, not a native descriptor.
    std::array<paint_vertex, 6> vertices;               ///< Ordered triangle-list geometry.
    bool operator==(const paint_quad&) const = default; ///< Exact deterministic output.
}; ///< Painter order is back-to-front; iterate in reverse for front-to-back candidates.

/// @brief Runtime scheduling policy fixed for one paint coordinator instance.
enum class paint_execution : std::uint8_t
{
    sequential, ///< Default until representative measurements justify parallel work.
    roots,      ///< Batch independent root groups; one root stays inline.
    chunks      ///< Tessellate bounded contiguous painter ranges after dependency resolution.
}; ///< Runtime policy fixed for one coordinator instance.

struct paint_limits
{
    std::size_t max_nodes = 65536;         ///< Captured node bound, including invisible containers.
    std::size_t min_parallel_quads = 4096; ///< Minimum surviving rectangles before dispatch.
    std::uint32_t max_chunks = 16;         ///< At most this many children plus one attached root.
    paint_execution execution = paint_execution::sequential; ///< Explicit opt-in execution.
}; ///< Admission limits are not whole-process memory quotas.

struct paint_statistics
{
    std::size_t visited_nodes = 0; ///< Prepared hierarchy size.
    std::size_t quads = 0;         ///< Nonempty visible rectangle count.
    std::size_t admitted_jobs = 0; ///< Accepted tasks, including the attached root.
    bool failed = false;           ///< Calculation returned no partial packet.
}; ///< Last owner-thread calculation observations.

namespace detail
{
class paint_data; ///< Allocator-retaining immutable packet storage.
}

class paint_snapshot
{
  public:
    paint_snapshot() = default;              ///< Empty packet with no source identity.
    std::uint64_t session() const noexcept;  ///< Source session or zero.
    std::uint64_t revision() const noexcept; ///< Geometry revision; not an appearance revision.
    layout_rect viewport() const noexcept;   ///< Logical viewport used for clipping and projection.
    std::span<const paint_quad> quads() const noexcept; ///< Stable immutable painter-order values.
  private:
    friend class paint_engine;                       ///< Only completed joined execution publishes.
    std::shared_ptr<const detail::paint_data> data_; ///< Copies share immutable PMR storage.
}; ///< Safe const reads after synchronized publication; no live tree or GPU ownership.

class paint_engine
{
  public:
    explicit paint_engine(jobs::job_dispatcher dispatcher, paint_limits limits = {},
                          std::shared_ptr<std::pmr::memory_resource> memory = {},
                          diagnostics::services destinations = {}); ///< Borrow one shared pool.
    ~paint_engine(); ///< Destroy on the owner outside callbacks and worker execution.
    paint_engine(const paint_engine&) = delete;            ///< Unique coordinator ownership.
    paint_engine& operator=(const paint_engine&) = delete; ///< No mutable aliasing.
    paint_snapshot calculate(const element_snapshot& input, const layout_snapshot& geometry,
                             const jobs::job_options& options = {}); ///< Join before publication.
    paint_statistics statistics() const; ///< Owner-only, non-reentrant observations.
  private:
    class impl;                  ///< Affinity, policy and optional services; no private scheduler.
    std::unique_ptr<impl> impl_; ///< Lifetime hidden from library consumers.
}; ///< Prepare stacking/clips serially; optionally tessellate resolved disjoint ranges in jobs.

layout_rect intersect_clip(layout_rect a, layout_rect b); ///< Checked rectangular intersection.
} // namespace atlas::ui
