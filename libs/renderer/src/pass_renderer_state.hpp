/** @file @brief Coordinator pipeline registry and transactional resource-state validation. */
#pragma once
#include "pass_backend.hpp"
#include "resource_store_state.hpp"

namespace atlas::renderer
{
/// @brief Pipeline metadata remains occupied until its last submission completes.
struct pipeline_entry
{
    std::shared_ptr<detail::gpu_pipeline> allocation; ///< Native immutable state.
    depth_mode depth =
        depth_mode::disabled;     ///< Attachment compatibility without retaining shader source.
    std::uint64_t generation = 1; ///< Nonwrapping slot lifetime.
    std::uint64_t last_use = 0;   ///< Protecting queue fence.
    bool live = false;            ///< Public identity validity.
};

/// @brief Prospective resource changes committed only after successful GPU submission.
struct scheduled_resource
{
    resource_state state; ///< State after all validated operations so far.
    bool initialized;     ///< Stored contents available to following operations.
    bool touched = false; ///< Extend the protecting fence when committed.
    bool written = false; ///< Publish a new initialization fence when committed.
};

/// @brief One pass renderer shares storage ownership but never creates a new device or scheduler.
class pass_renderer::impl
{
  public:
    impl(std::shared_ptr<resource_store::impl> store, pass_limits budgets)
        : resources(std::move(store)), limits(budgets),
          backend(detail::make_pass_backend(resources->backend, limits)),
          pipelines(resources->backend->memory())
    {
        pipelines.resize(limits.max_pipelines);
        diagnostics::write(resources->destinations, diagnostics::log_level::info, "renderer.passes",
                           "Pass renderer initialized");
    } ///< Reserve the registry.

    ~impl()
    {
        const diagnostics::operation_scope scope{resources->destinations,
                                                 {"pass_renderer.shutdown", "renderer.cpu_wait"}};
        resources->backend->drain();
        diagnostics::write(resources->destinations, diagnostics::log_level::info, "renderer.passes",
                           "Pass renderer drained");
    } ///< Complete work before native state release.

    pipeline_entry& get(pipeline_handle handle); ///< Check provenance, generation and coordinator.
    detail::resolved_draw resolve_draw(
        const draw_packet& draw, const detail::resolved_pass& pass,
        std::vector<scheduled_resource>& states,
        std::vector<std::uint32_t>& descriptors); ///< Validate one draw's bindings and ranges.
    std::vector<detail::resolved_pass>
    resolve(const frame_plan& frame, std::vector<scheduled_resource>& states,
            std::vector<std::uint32_t>& descriptors); ///< Validate without GPU side effects.
    std::shared_ptr<resource_store::impl> resources; ///< Retain device and all resource registries.
    pass_limits limits;                              ///< Bounded admission policy.
    std::unique_ptr<detail::pass_backend> backend;   ///< Selected platform adapter.
    std::pmr::vector<pipeline_entry> pipelines;      ///< Includes fence-retired entries.
    std::uint64_t identity = detail::next_identity(); ///< Pipeline provenance.
};
} // namespace atlas::renderer
