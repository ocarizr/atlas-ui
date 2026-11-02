/** @file @brief SDK-free resolved pass commands passed to a native adapter. */
#pragma once
#include "resource_backend.hpp"
#include <atlas/renderer/passes.hpp>

namespace atlas::renderer::detail
{
/// @brief Native immutable pipeline with reflected binding requirements.
struct gpu_pipeline
{
    virtual ~gpu_pipeline() = default; ///< Release after its protecting fence.
    bool textured = false;             ///< Compiled pixel shader actually reads t0.
};

/// @brief Resource identity and the state expected before one ordered operation.
struct pass_resource
{
    std::shared_ptr<gpu_resource> allocation; ///< Recording-time borrowed ownership.
    resource_state before = resource_state::copy_destination; ///< Scheduled prior state.
};

/// @brief Validated draw with native identities and portable immutable parameters.
struct resolved_draw
{
    draw_packet packet;                     ///< Validated numeric draw parameters.
    std::shared_ptr<gpu_pipeline> pipeline; ///< Native state object.
    pass_resource vertices;                 ///< Vertex input and prior state.
    std::optional<pass_resource> texture;   ///< Optional texture requiring a state transition.
};

/// @brief Validated attachments and ordered draws for one pass.
struct resolved_pass
{
    pass_description description;       ///< Attachment operations and draw order.
    texture_description extent;         ///< Color extent.
    pass_resource color;                ///< Color allocation and prior state.
    std::optional<pass_resource> depth; ///< Depth allocation and prior state.
    std::vector<resolved_draw> draws;   ///< Fully resolved before native recording.
};

/// @brief Coordinator-only adapter with bounded in-flight command ownership.
class pass_backend
{
  public:
    virtual ~pass_backend() = default; ///< Drain before native release.
    virtual std::shared_ptr<gpu_pipeline>
    create(const pipeline_description& description) = 0; ///< Compile and reflect.
    virtual std::uint64_t
    submit(std::span<const resolved_pass> passes) = 0; ///< Record after complete validation.
    virtual void collect() = 0;                        ///< Reuse only completed command objects.
};

std::unique_ptr<pass_backend>
make_pass_backend(const std::shared_ptr<resource_backend>& resources,
                  pass_limits limits); ///< Select native implementation.
} // namespace atlas::renderer::detail
