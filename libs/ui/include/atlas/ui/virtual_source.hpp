/**
 * @file
 * @brief Immutable logical list identity and exact geometry without retained widgets.
 */
#pragma once
#include <atlas/ui/layout_types.hpp>
#include <optional>
#include <vector>

namespace atlas::ui
{
using item_key = std::uint64_t; ///< Stable nonzero application identity, independent of row slots.

/// @brief Source-owned exact row geometry and binding revision.
struct logical_item
{
    item_key key = 0;           ///< Unique within one immutable source.
    float offset = 0;           ///< Leading position in the complete logical content.
    float height = 0;           ///< Strictly positive exact outer row height.
    std::uint64_t revision = 0; ///< Change when bindings/resources must be recreated.
};

/// @brief Thread-safe immutable input; implementations must not access storage or mutate widgets.
class item_source
{
  public:
    virtual ~item_source() = default; ///< Retain through every joined metadata operation.
    virtual std::size_t count() const noexcept = 0;         ///< Logical count, not realized count.
    virtual float extent() const noexcept = 0;              ///< Finite exact total logical height.
    virtual logical_item item(std::size_t index) const = 0; ///< Checked logical index lookup.
    virtual std::optional<std::size_t> find(item_key key) const = 0; ///< Stable-key lookup.
    virtual std::size_t locate(float position) const = 0; ///< First row whose end exceeds position.
};

/// @brief Arithmetic source configuration with constant metadata storage.
struct fixed_item_options
{
    std::size_t count = 0; ///< Keys are one-based logical indices.
    float height = 20;     ///< Positive nominal height; effective endpoints use float coordinates.
};

/// @brief Constant-storage source for immutable one-based keys and a nominal uniform height.
class fixed_item_source final : public item_source
{
  public:
    explicit fixed_item_source(fixed_item_options options); ///< Validate numeric precision/range.
    std::size_t count() const noexcept override;            ///< Return the configured count.
    float extent() const noexcept override;              ///< Count multiplied by exact row height.
    logical_item item(std::size_t index) const override; ///< Arithmetic position and stable key.
    std::optional<std::size_t>
    find(item_key key) const override;                 ///< Constant-time identity lookup.
    std::size_t locate(float position) const override; ///< Half-open range lookup.
  private:
    fixed_item_options options_; ///< No allocation per logical item.
};

/// @brief Authoritative data supplied after measuring or loading application metadata.
struct item_description
{
    item_key key = 0;           ///< Unique nonzero identity, retained across insertion/reorder.
    float height = 0;           ///< Exact positive height, including row decoration.
    std::uint64_t revision = 0; ///< Binding revision, independent of position.
};

/// @brief Exact variable-height prefix index; O(N) source storage is separate from realization.
class variable_item_source final : public item_source
{
  public:
    explicit variable_item_source(std::span<const item_description> items,
                                  std::shared_ptr<std::pmr::memory_resource> memory =
                                      {});               ///< Copy a validated immutable index.
    std::size_t count() const noexcept override;         ///< Number of logical rows.
    float extent() const noexcept override;              ///< Sum of exact supplied heights.
    logical_item item(std::size_t index) const override; ///< Checked prefix geometry lookup.
    std::optional<std::size_t> find(item_key key) const override; ///< Logarithmic key lookup.
    std::size_t locate(float position) const override; ///< Logarithmic half-open extent lookup.
  private:
    std::shared_ptr<std::pmr::memory_resource> memory_; ///< Outlives both PMR vectors.
    std::pmr::vector<logical_item> items_; ///< Exact prefix geometry, in logical order.
    std::pmr::vector<std::pair<item_key, std::size_t>> keys_; ///< Sorted stable identity index.
    float extent_ = 0; ///< Finite representable total height.
};
} // namespace atlas::ui
