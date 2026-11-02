/** @file @brief D3D12 pipeline and bounded pass-recording implementation. */
#pragma once
#include "native_resources.hpp"
#include "pass_backend.hpp"
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
    std::uint64_t fence = 0;                     ///< Protecting submission value.
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
    submit(std::span<const resolved_pass> passes) override; ///< Execute one ordered frame.
    void collect() override;                                ///< Reclaim only completed batches.
    pass_batch begin(); ///< Reuse completed storage or admit a new bounded batch.
    void record(pass_batch& batch, const resolved_pass& pass,
                std::uint32_t index);            ///< Record one explicit pass.
    std::shared_ptr<native_resources> resources; ///< Pins queue, PMR and SRV heap.
    pass_limits limits;                          ///< Fixed recording capacities.
    std::pmr::vector<pass_batch> pending;        ///< Pre-reserved in-flight batches.
    std::pmr::vector<pass_batch> reusable;       ///< Fence-completed reusable batches.
};
} // namespace atlas::renderer::detail
