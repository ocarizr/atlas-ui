/** @file @brief D3D12 pipeline and bounded pass-recording implementation. */
#pragma once
#include "native_resources.hpp"
#include "pass_backend.hpp"
#include "recording_plan.hpp"
#include <dxcapi.h>

namespace atlas::renderer::detail
{
/// @brief Compiled native pipeline and its fixed root ABI.
struct native_pipeline final : gpu_pipeline
{
    com_ptr<ID3D12RootSignature> root;  ///< b0 constants, pixel t0, static s0.
    com_ptr<ID3D12PipelineState> state; ///< Immutable graphics state.
};

/// @brief Compiler output includes reflection-derived texture requirements.
struct compiled_shader
{
    com_ptr<IDxcBlob> code; ///< DXIL executable bytes.
    bool textured = false;  ///< Pixel shader reads the validated t0 binding.
};

/// @brief Named compiler inputs avoid interchangeable source/entry arguments.
struct shader_input
{
    std::string_view source; ///< Borrowed HLSL source.
    std::string_view entry;  ///< Validated entry identifier.
    bool pixel = false;      ///< Pixel or vertex stage.
};

compiled_shader
compile_pass_shader(shader_input input); ///< Compile memory only and validate resource ABI.
std::shared_ptr<gpu_pipeline> create_pass_pipeline(
    native_device& context,
    const pipeline_description& description); ///< Build native state before publishing identity.

/// @brief Exclusive recording state retained until its queue fence completes.
struct pass_batch
{
    com_ptr<ID3D12CommandAllocator> allocator;   ///< Never reset before completion.
    com_ptr<ID3D12GraphicsCommandList> commands; ///< Exclusive command list.
    com_ptr<ID3D12DescriptorHeap> colors;        ///< CPU RTV slots for this batch.
    com_ptr<ID3D12DescriptorHeap> depths;        ///< CPU DSV slots for this batch.
};

/// @brief All lists belonging to one transaction share a protecting queue value.
struct pass_frame
{
    explicit pass_frame(std::pmr::memory_resource* memory) : batches(memory)
    {
    } ///< Retain selected metadata allocator.

    std::pmr::vector<pass_batch> batches; ///< Exclusive contexts, grown only before execution.
    std::uint64_t fence = 0;              ///< Protects every context in this frame.
};

/// @brief Ordered direct-queue pass execution sharing the resource store's device and SRV heap.
class native_passes final : public pass_backend
{
  public:
    native_passes(std::shared_ptr<native_resources> resources,
                  pass_limits limits); ///< Reserve bounded batch metadata.

    ~native_passes() override
    {
        resources->drain();
    } ///< Complete before command storage release.

    std::shared_ptr<gpu_pipeline> create(const pipeline_description& description) override
    {
        return create_pass_pipeline(*resources->context, description);
    } ///< Compile on the coordinator.

    std::uint64_t
    submit(std::span<const resolved_pass> passes, recording_executor* executor,
           recording_options options,
           const diagnostics::services& destinations) override; ///< Join before ordered execution.

    recording_statistics last_recording() const override
    {
        return statistics;
    } ///< Borrow last accepted counts.

    void collect() override;             ///< Reclaim only completed batches.
    pass_frame begin(std::size_t count); ///< Reuse completed storage or admit exclusive contexts.
    void record(pass_batch& batch, const resolved_pass& pass,
                const recording_section& section); ///< Record one contiguous pass section.
    std::shared_ptr<native_resources> resources;   ///< Pins queue, PMR and SRV heap.
    pass_limits limits;                            ///< Fixed recording capacities.
    std::pmr::vector<pass_frame> pending;          ///< Pre-reserved in-flight frames.
    std::pmr::vector<pass_frame> reusable;         ///< Fence-completed reusable frames.
    recording_statistics statistics; ///< Published only after successful queue submission.
};
} // namespace atlas::renderer::detail
