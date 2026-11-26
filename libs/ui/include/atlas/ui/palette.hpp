/**
 * @file
 * @brief Immutable semantic colors and explicit owner-thread theme selection.
 */
#pragma once
#include <array>
#include <atlas/diagnostics/instrumentation.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace atlas::ui
{
using rgba = std::array<float, 4>;  ///< Straight-alpha linear RGBA, all channels in [0,1].
using palette_slot = std::uint16_t; ///< Sorted index, meaningful only in a compatible contract.
inline constexpr std::size_t max_palette_colors = 256; ///< Bounded immutable snapshot size.
[[nodiscard]] bool
valid_palette_key(std::string_view value) noexcept; ///< Bounded ASCII identifier.

/// @brief A stable ASCII semantic key and its compiled linear color.
struct palette_color
{
    std::string key; ///< Case-sensitive identifier, at most 64 characters.
    rgba value;      ///< Linear RGB and straight alpha; no implicit color conversion.
};

/// @brief Native/prepared source, validated by the same compiler as XML-derived definitions.
struct palette_definition
{
    std::string contract;              ///< Nonempty contract identifier, at most 64 characters.
    std::vector<palette_color> colors; ///< Complete set, never a sparse theme instance.
};

/// @brief Validated immutable snapshot; safe to retain on readers without a context lock.
class palette
{
  public:
    [[nodiscard]] static std::shared_ptr<const palette>
    compile(palette_definition source); ///< Reject duplicates, nonfinite values and limits.
    [[nodiscard]] palette_slot find(std::string_view key) const; ///< Resolve once or throw.
    [[nodiscard]] const rgba& color(palette_slot slot) const;    ///< Checked constant-time lookup.
    [[nodiscard]] const palette_definition& definition() const noexcept; ///< Sorted native data.
    [[nodiscard]] bool compatible(const palette& other) const noexcept;  ///< Exact name/key set.

  private:
    explicit palette(palette_definition source); ///< Store only a fully validated definition.
    palette_definition source_; ///< Snapshot owns its compact sorted table and readable keys.
};

/// @brief Explicit noncopyable owner-affine selection; no mutable global theme state.
class palette_context
{
  public:
    explicit palette_context(std::shared_ptr<const palette> initial,
                             diagnostics::services sinks = {}); ///< Require a complete palette.
    palette_context(const palette_context&) = delete; ///< Context identity belongs to its host.
    palette_context& operator=(const palette_context&) = delete; ///< No shared mutable copies.
    void select(std::shared_ptr<const palette> candidate); ///< Commit compatible snapshot only.
    [[nodiscard]] std::shared_ptr<const palette> snapshot() const; ///< Capture on the owner.
    [[nodiscard]] std::uint64_t revision() const; ///< One-based; no-op reselection is unchanged.

  private:
    void require_owner() const; ///< Reject worker use before reading mutable selection state.
    std::thread::id owner_;     ///< Creating thread owns selection and snapshot capture.
    std::shared_ptr<const palette> selected_; ///< Last compatible immutable snapshot.
    diagnostics::services sinks_;             ///< Optional logging and frame-boundary capture.
    std::uint64_t revision_ = 1; ///< Never wraps or reuses an old publication identity.
};
} // namespace atlas::ui
