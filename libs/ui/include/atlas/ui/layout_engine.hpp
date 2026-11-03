/**
 * @file
 * @brief Bounded measure/arrange over immutable forests with root-level incremental reuse.
 */
#pragma once
#include <atlas/jobs/job_dispatcher.hpp>
#include <atlas/ui/element_snapshot.hpp>
#include <atlas/ui/layout_snapshot.hpp>

namespace atlas::ui
{
/// @brief Explicit work limits, not global memory quotas.
struct layout_limits
{
    std::size_t max_nodes = 65536;         ///< Supported range is 1..65536.
    std::size_t min_parallel_nodes = 4096; ///< Small forests stay on the coordinator.
    std::uint32_t max_chunks = 16;         ///< Root chunk cap; one disables job admission.
};

/// @brief Observations of the latest calculation.
struct layout_statistics
{
    std::size_t measured_nodes = 0; ///< Nodes measured after exact cache comparison.
    std::size_t arranged_nodes = 0; ///< Nodes assigned rectangles.
    std::size_t cached_roots = 0;   ///< Roots reused without numeric layout work.
    std::size_t admitted_jobs = 0;  ///< Accepted jobs across both joined phases.
    bool failed = false;            ///< No partial result was returned.
};

/// @brief Owner-only coordinator borrowing the application's persistent pool.
class layout_engine
{
  public:
    explicit layout_engine(jobs::job_dispatcher dispatcher, layout_limits limits = {},
                           std::shared_ptr<std::pmr::memory_resource> memory = {},
                           diagnostics::services destinations = {}); ///< Reserve metadata.
    ~layout_engine();                             ///< Destroy on the owner outside callbacks.
    layout_engine(const layout_engine&) = delete; ///< One cache owner.
    layout_engine& operator=(const layout_engine&) = delete; ///< Never alias cache mutation.
    layout_snapshot
    calculate(const element_snapshot& snapshot,
              const jobs::job_options& options = {}); ///< Return complete immutable geometry.
    layout_snapshot
    update(element_tree& tree, layout_rect viewport,
           const jobs::job_options& options = {}); ///< Capture, calculate and publish.
    layout_statistics statistics() const;          ///< Owner-only, non-reentrant observations.
  private:
    class impl;                  ///< Exact root cache, iterative kernels and injected phases.
    std::unique_ptr<impl> impl_; ///< No hidden pool, native window or graphics resources.
};
} // namespace atlas::ui
