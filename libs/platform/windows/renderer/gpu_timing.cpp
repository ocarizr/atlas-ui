/** @file @brief Actual GPU queue timestamps, reported as durations without fabricated CPU
 * alignment. */
#include "gpu_timing.hpp"
#include <cstring>

namespace atlas::renderer::detail
{
/// @brief Optional diagnostics cannot turn a query allocation failure into a rendering failure.
void gpu_frame_timer::prepare(native_device& device, const diagnostics::services& destinations)
{
#if ATLAS_PROFILING_ENABLED
    destinations_ = destinations;
    if (!destinations_.profile)
    {
        return;
    }
    try
    {
        if (!queries_)
        {
            check(device.queue->GetTimestampFrequency(&frequency_),
                  "query GPU timestamp frequency");
            if (frequency_ == 0)
            {
                throw std::runtime_error("zero GPU timestamp frequency");
            }

            D3D12_QUERY_HEAP_DESC queries{};
            queries.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            queries.Count = 2;
            check(device.gpu->CreateQueryHeap(&queries, IID_PPV_ARGS(&queries_)),
                  "create GPU timestamp queries");
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC buffer{};
            buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            buffer.Width = 16;
            buffer.Height = 1;
            buffer.DepthOrArraySize = 1;
            buffer.MipLevels = 1;
            buffer.SampleDesc.Count = 1;
            buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            check(device.gpu->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                                                      D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                      IID_PPV_ARGS(&readback_)),
                  "create GPU timestamp readback");
        }
    }
    catch (...)
    {
        queries_.Reset();
        readback_.Reset();
        diagnostics::write(destinations_, diagnostics::log_level::warning, "renderer.gpu",
                           "GPU timing allocation unavailable");
        destinations_ = {};
    }
#else
    (void)device;
    (void)destinations;
#endif
}

/// @brief Each submitted frame starts on the first ordered command list.
void gpu_frame_timer::begin(ID3D12GraphicsCommandList& commands) const
{
#if ATLAS_PROFILING_ENABLED
    if (destinations_.profile)
    {
        commands.EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
    }
#else
    (void)commands;
#endif
}

/// @brief Only the final list resolves data, keeping parallel recorders on disjoint query indices.
void gpu_frame_timer::end(ID3D12GraphicsCommandList& commands) const
{
#if ATLAS_PROFILING_ENABLED
    if (destinations_.profile)
    {
        commands.EndQuery(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        commands.ResolveQueryData(queries_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback_.Get(),
                                  0);
    }
#else
    (void)commands;
#endif
}

/// @brief Counter time denotes collection; value measures GPU work and is never relabeled CPU time.
std::optional<double> gpu_frame_timer::collect()
{
#if ATLAS_PROFILING_ENABLED
    if (destinations_.profile)
    {
        auto destinations = std::move(destinations_);
        const D3D12_RANGE range{0, 16};
        void* mapped = nullptr;
        if (SUCCEEDED(readback_->Map(0, &range, &mapped)))
        {
            std::array<std::uint64_t, 2> ticks{};
            std::memcpy(ticks.data(), mapped, sizeof(ticks));
            const D3D12_RANGE written{0, 0};
            readback_->Unmap(0, &written);
            if (ticks[1] >= ticks[0])
            {
                const auto duration = static_cast<double>(ticks[1] - ticks[0]) * 1e6 /
                                      static_cast<double>(frequency_);
                (void)destinations.profile->counter("renderer.gpu.duration_us", "renderer.gpu",
                                                    duration);
                return duration;
            }
        }

        diagnostics::write(destinations, diagnostics::log_level::warning, "renderer.gpu",
                           "GPU timing sample unavailable");
    }
#endif
    return std::nullopt;
}
} // namespace atlas::renderer::detail
