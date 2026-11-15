/** @file @brief Build-local resolution of material defaults, overrides and dependencies. */
#pragma once
#include <atlas/ui_content/instance.hpp>
#include <map>

namespace atlas::ui_content::detail
{
/// @brief Per-element defaults copied into runtime state before animation.
struct resolved_material
{
    ui::material_style style;                 ///< Shader key and resolved typed values.
    std::optional<assets::asset_ref> texture; ///< Persistent image supplied to the host.
};

/// @brief Resolve each descriptor and shader pairing at most once in a private candidate.
class material_sources
{
  public:
    explicit material_sources(const construction& options); ///< Index borrowed prepared values.
    /// @brief Resolve one template or sparse override and its mounted dependencies.
    [[nodiscard]] const resolved_material& resolve(assets::asset_ref source);

    std::map<assets::asset_ref, streaming::read_stamp> dependencies; ///< Exact mount stamps.
    std::vector<material_shader_source> shaders; ///< Owned source for the graphics coordinator.

  private:
    [[nodiscard]] const material_document& load(assets::asset_ref source); ///< Read once.
    /// @brief Acquire trusted HLSL once per shader and ABI pairing.
    [[nodiscard]] std::uint64_t shader(assets::asset_ref source, ui::material_kind kind);
    void depend_on(assets::asset_ref source); ///< Track a live typed dependency.
    const construction& options_;             ///< Borrowed owner services during composition only.
    std::map<assets::asset_ref, prepared_material> prepared_; ///< Revision-checked native values.
    std::map<assets::asset_ref, material_document> descriptors_; ///< Parsed document cache.
    std::map<assets::asset_ref, resolved_material> instances_;   ///< Inherited authored defaults.
    /// @brief Reuse one key per shader source and ABI inside the candidate.
    std::map<std::pair<assets::asset_ref, ui::material_kind>, std::uint64_t> shader_keys_;
    std::size_t shader_bytes_ = 0; ///< Aggregate budget for retained custom HLSL.
};
} // namespace atlas::ui_content::detail
