/**
 * @file
 * @brief Immutable ordered phase output retaining its frame identity and allocation policy.
 */
#pragma once
#include <cstdint>
#include <memory>
#include <memory_resource>
#include <span>
#include <vector>

namespace atlas::ui
{
class frame_executor; ///< Owner-thread coordinator publishing joined results.

/// @brief Explicit publication identity; zero denotes an empty or moved-from result.
struct phase_identity
{
    std::uint64_t frame = 0;   ///< Application-chosen increasing frame identity.
    std::uint32_t ordinal = 0; ///< One-based attempted phase within that frame.
    bool operator==(const phase_identity&) const = default; ///< Compare the complete identity.
};

namespace detail
{
/// @brief Only the executing phase mutates this storage before publication.
template <typename T> struct phase_storage
{
    phase_storage(std::shared_ptr<std::pmr::memory_resource> resource, phase_identity id,
                  std::size_t count)
        : memory(std::move(resource)), identity(id), values(0, memory.get())
    {
        values.resize(count);
    } ///< Construct outputs before job admission; destruction retains the resource.

    std::shared_ptr<std::pmr::memory_resource>
        memory;                 ///< Outlives all values and container metadata.
    phase_identity identity;    ///< Immutable once the result is published.
    std::pmr::vector<T> values; ///< Exclusive slices during execution; const reads after join.
};
} // namespace detail

/// @brief Copies retain immutable values; publication to another thread still needs
/// synchronization.
template <typename T> class phase_results
{
  public:
    phase_results() = default; ///< Empty identity-zero result, never a failed partial publication.

    phase_identity identity() const noexcept
    {
        return data_ ? data_->identity : phase_identity{};
    } ///< Identify this joined phase without inspecting worker completion order.

    std::span<const T> values() const noexcept
    {
        return data_ ? std::span<const T>{data_->values} : std::span<const T>{};
    } ///< Borrow only while at least one result copy remains alive.
  private:
    friend class frame_executor; ///< Only a completed execution can publish mutable storage.

    explicit phase_results(std::shared_ptr<const detail::phase_storage<T>> data)
        : data_(std::move(data))
    {
    } ///< Retain the prepared allocation and its immutable identity.

    std::shared_ptr<const detail::phase_storage<T>> data_; ///< Shared immutable payload ownership.
};
} // namespace atlas::ui
