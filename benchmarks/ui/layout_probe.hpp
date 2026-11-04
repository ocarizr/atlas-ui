/** @file @brief Layout benchmark state and requested PMR accounting, separate from process RSS. */
#pragma once
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <atomic>

namespace atlas::ui::benchmarks
{
/// @brief Thread-safe requested-byte counters exclude allocator caches and non-PMR heaps.
class probe_resource final : public std::pmr::memory_resource
{
  public:
    std::atomic<std::size_t> live{0};  ///< Current successful requested bytes.
    std::atomic<std::size_t> peak{0};  ///< Maximum simultaneously requested bytes.
    std::atomic<std::size_t> calls{0}; ///< Allocation requests in the census interval.

    void reset()
    {
        calls = 0;
        peak = live.load();
    } ///< Reset only after the previous calculation joined.

  private:
    void* do_allocate(std::size_t bytes, std::size_t alignment) override
    {
        auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        ++calls;
        const auto current = live.fetch_add(bytes) + bytes;
        auto maximum = peak.load();
        while (maximum < current && !peak.compare_exchange_weak(maximum, current))
        {
        }

        return result;
    } ///< Atomics allow existing independent-root kernels to use the same resource.

    void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override
    {
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
        live.fetch_sub(bytes);
    } ///< Counter lifetime is retained by every result.

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override
    {
        return this == &other;
    } ///< No mutable test-global instrumentation.
};

/// @brief Fixed reproducible scenario shape, separate from scheduling policy.
struct layout_case
{
    const char* name;        ///< Stable scenario identity.
    std::uint32_t panels;    ///< Independent sibling panels per root.
    std::uint32_t leaves;    ///< Lightweight children per panel.
    std::uint32_t depth = 0; ///< Unary wrappers before panel fan-out.
    std::uint32_t roots = 1; ///< Independently cached roots.
    bool related = false;    ///< Directional canvas dependencies affect arrangement only.
    bool unchanged = false;  ///< Exercise cached calculation while still capturing input.
};

/// @brief Policy states share the application's worker pool and identical logical inputs.
struct layout_probe
{
    layout_probe(jobs::job_dispatcher dispatcher, layout_case scenario, layout_limits policy,
                 diagnostics::services destinations); ///< Build outside timed intervals.
    std::shared_ptr<probe_resource> memory = std::make_shared<probe_resource>(); ///< PMR lifetime.
    element_tree tree;                   ///< Separate session for each policy.
    layout_engine engine;                ///< Persistent exact cache.
    element_handle changed;              ///< Same logical leaf is changed per frame.
    std::vector<element_handle> changes; ///< One leaf per root for a fully dirty forest.
    bool unchanged = false;              ///< Cached-case mutation policy.
    layout_snapshot last;                ///< Keep exactly one consumer result per policy.
    std::size_t roots = 1;               ///< Used to report the per-phase live-task upper bound.
    std::uint32_t max_chunks = 1; ///< Scheduling cap, distinct from actual total admitted jobs.
};

int run_layout_benchmark(bool smoke, bool profile); ///< XML output; no timing threshold assertions.
} // namespace atlas::ui::benchmarks
