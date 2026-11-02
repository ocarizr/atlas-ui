/**
 * @file
 * @brief Immutable generic draw packets, fixed shader ABI and ordered offscreen passes.
 */
#pragma once
#include <atlas/renderer/recording.hpp>
#include <atlas/renderer/resources.hpp>
#include <optional>

namespace atlas::renderer
{
/// @brief Fixed interleaved vertex ABI used by generic 2D and 3D geometry.
struct vertex
{
    std::array<float, 3> position{};        ///< Clip-space input before the draw transform.
    std::array<float, 4> color{1, 1, 1, 1}; ///< Linear modulation color.
    std::array<float, 2> uv{};              ///< Texture coordinates.
};
/// @brief Builtin shaders or caller-provided in-memory HLSL using the same ABI.
enum class shader_program : std::uint8_t
{
    colored,  ///< Interpolated vertex color.
    textured, ///< Texture multiplied by interpolated color.
    custom    ///< Trusted HLSL; b0 transform/tint, pixel t0 and s0 only.
};
/// @brief Color composition policy, independent of UI concepts.
enum class blend_mode : std::uint8_t
{
    opaque,             ///< Replace destination.
    straight_alpha,     ///< Multiply source RGB by source alpha.
    premultiplied_alpha ///< Source RGB already includes alpha.
};
/// @brief Optional D32 depth testing.
enum class depth_mode : std::uint8_t
{
    disabled,   ///< No depth attachment.
    read_write, ///< Less-or-equal comparison and depth writes.
    read_only   ///< Less-or-equal comparison without depth writes.
};

/// @brief Immutable pipeline creation input; no shader file access is performed.
struct pipeline_description
{
    shader_program program = shader_program::colored; ///< Builtin default.
    blend_mode blend = blend_mode::opaque;            ///< Color blend equation.
    depth_mode depth = depth_mode::disabled;          ///< Depth attachment compatibility.
    bool linear_sampling = true;              ///< Clamp sampler uses linear or nearest filtering.
    std::string source;                       ///< Custom shader source, bounded to 1 MiB.
    std::string vertex_entry = "vertex_main"; ///< Custom vertex entry identifier.
    std::string pixel_entry = "pixel_main";   ///< Custom pixel entry identifier.
    void validate() const; ///< Reject unsupported enums and malformed shader inputs.
};
/// @brief Distinct identity for fence-retired graphics pipelines.
struct pipeline_tag;
/// @brief Renderer-local generation and provenance.
using pipeline_handle = resource_handle<pipeline_tag>;

/// @brief Integer pixel rectangle for a draw's optional scissor.
struct pixel_rectangle
{
    std::uint32_t left = 0;   ///< Inclusive left edge.
    std::uint32_t top = 0;    ///< Inclusive top edge.
    std::uint32_t right = 0;  ///< Exclusive right edge.
    std::uint32_t bottom = 0; ///< Exclusive bottom edge.
};

/// @brief Pixel viewport; depth maps to the standard zero-to-one interval.
struct pixel_viewport
{
    float x = 0;      ///< Left edge.
    float y = 0;      ///< Top edge.
    float width = 0;  ///< Positive width within the attachment.
    float height = 0; ///< Positive height within the attachment.
};

/// @brief Value-only triangle-list command; handles resolve only on the coordinator.
struct draw_packet
{
    pipeline_handle pipeline;       ///< Pipeline owner and generation.
    buffer_handle vertices;         ///< Fully initialized interleaved vertex buffer.
    descriptor_handle texture;      ///< Required only when the shader reads t0.
    std::uint32_t first_vertex = 0; ///< Starting vertex in the buffer.
    std::uint32_t vertex_count = 0; ///< Positive multiple of three.
    std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0,
                                    0, 0, 1, 0, 0, 0, 0, 1}; ///< Row-major b0 matrix.
    std::array<float, 4> tint{1, 1, 1, 1};                   ///< b0 color multiplier.
    std::optional<pixel_rectangle> scissor;                  ///< Defaults to the full attachment.
    std::optional<pixel_viewport> viewport;                  ///< Defaults to the full attachment.
};
/// @brief Attachment initialization contract.
enum class load_action : std::uint8_t
{
    clear, ///< Initialize the entire attachment.
    load   ///< Require previously initialized stored contents.
};
/// @brief Whether later passes may observe the resulting attachment contents.
enum class store_action : std::uint8_t
{
    store,  ///< Publish initialized contents.
    discard ///< Contents become undefined after this pass.
};

/// @brief Single RGBA8 color attachment with explicit load/store semantics.
struct color_attachment
{
    texture_handle target;                        ///< Must have color_target role.
    load_action load = load_action::clear;        ///< Initialization policy.
    std::array<float, 4> clear_color{0, 0, 0, 1}; ///< Finite normalized RGBA.
    store_action store = store_action::store;     ///< Visibility to subsequent passes.
};

/// @brief Optional D32 attachment matching the color extent.
struct depth_attachment
{
    texture_handle target;                    ///< Must have depth_target role.
    load_action load = load_action::clear;    ///< Initialization policy.
    float clear_depth = 1;                    ///< Finite value in [0,1].
    store_action store = store_action::store; ///< Visibility to subsequent passes.
};

/// @brief Ordered draws sharing attachment and load/store state.
struct pass_description
{
    color_attachment color;                ///< Required color output.
    std::optional<depth_attachment> depth; ///< Optional matching depth output.
    std::vector<draw_packet> draws;        ///< Stable submission order.
};

/// @brief Deep immutable snapshot, shareable between CPU preparation and submission.
class frame_plan
{
  public:
    explicit frame_plan(std::span<const pass_description> passes); ///< Copy all nested data.
    std::span<const pass_description> passes() const noexcept; ///< Borrow immutable pass values.
  private:
    std::shared_ptr<const std::vector<pass_description>> passes_; ///< No writable aliases escape.
};

/// @brief Hard limits include retired pipelines and in-flight command batches.
struct pass_limits
{
    std::uint32_t max_pipelines = 64;        ///< Pipeline slots.
    std::uint32_t max_passes = 32;           ///< Passes per submission.
    std::uint32_t max_draws = 4096;          ///< Draws across one submission.
    std::uint32_t max_frames = 3;            ///< In-flight submissions, with nonblocking admission.
    std::uint32_t max_recording_batches = 8; ///< Exclusive contexts per in-flight frame.
    void validate() const;                   ///< Reject unbounded or empty capacities.
};

/// @brief Device-coordinator pass execution retaining resources through queue completion.
class pass_renderer
{
  public:
    explicit pass_renderer(resource_store& resources,
                           pass_limits limits = {}); ///< Retain store state.
    ~pass_renderer(); ///< Drain before releasing native pipelines and command storage.
    pass_renderer(const pass_renderer&) = delete;            ///< Unique coordinator ownership.
    pass_renderer& operator=(const pass_renderer&) = delete; ///< Unique pipeline identity.
    pipeline_handle
    create_pipeline(const pipeline_description& description); ///< Compile before publishing.
    void release(pipeline_handle pipeline); ///< Invalidate now, reclaim after its last fence.
    void collect(); ///< Reclaim completed pipeline and command storage without waiting.
    completion_token
    submit(const frame_plan& frame); ///< Validate the whole frame before executing.
    completion_token
    submit(const frame_plan& frame, recording_executor& executor,
           recording_options options = {}); ///< Join coarse batches before stable GPU submission.
    recording_statistics last_recording() const; ///< Last successful recording, coordinator only.
  private:
    class impl;                  ///< Platform-neutral registry and selected native pass backend.
    std::unique_ptr<impl> impl_; ///< Coordinator-owned state.
};
} // namespace atlas::renderer
