/** @file @brief Bounded per-frame native timestamp/readback ownership. */
#pragma once
#include "native_renderer.hpp"
#include <optional>

namespace atlas::renderer::detail
{
/// @brief One reusable query pair; CPU inspection is permitted only after the frame fence.
class gpu_frame_timer
{
  public:
    void prepare(native_device& device,
                 const diagnostics::services&
                     destinations); ///< Allocate lazily only with compiled/injected profiling.
    void
    begin(ID3D12GraphicsCommandList& commands) const; ///< Stamp before the first frame command.
    void end(ID3D12GraphicsCommandList& commands)
        const;                       ///< Resolve both timestamps after the final frame command.
    std::optional<double> collect(); ///< Emit a real GPU duration counter after native completion.
  private:
    com_ptr<ID3D12QueryHeap> queries_; ///< Two independent timestamp indices.
    com_ptr<ID3D12Resource>
        readback_;                ///< Sixteen payload bytes in bounded native readback storage.
    std::uint64_t frequency_ = 0; ///< Queue ticks per second, not GPU frequency or CPU wall time.
    diagnostics::services destinations_; ///< Retained only until the protecting fence is harvested.
};
} // namespace atlas::renderer::detail
