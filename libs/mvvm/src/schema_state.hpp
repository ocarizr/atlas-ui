/** @file @brief Immutable schema layout and shared scalar/name validation. */
#pragma once
#include <array>
#include <atlas/mvvm/schema.hpp>
#include <stdexcept>

namespace atlas::mvvm::detail
{
/// @brief Immutable copied metadata plus offsets into per-type instance arrays.
struct schema_data
{
    definition source; ///< Editor/generated metadata copy.
    std::size_t max_arguments =
        0;                ///< Actual largest signal, avoiding unused per-instance scratch capacity.
    schema_limits limits; ///< Validation policy retained for runtime values.
    std::uint64_t id;     ///< Unique in-process generation.
    std::vector<std::uint32_t> offsets;  ///< Field index to typed column offset.
    std::array<std::size_t, 4> counts{}; ///< Typed array sizes.
    std::vector<std::pair<std::string, std::uint32_t>> fields, actions,
        events; ///< Sorted name indexes.
};

/// @brief Restore an owner-only operation flag during exception unwinding.
struct flag_guard
{
    bool& flag; ///< Borrowed guard state.

    explicit flag_guard(bool& state) : flag(state)
    {
        flag = true;
    } ///< Enter the guarded operation.

    ~flag_guard()
    {
        flag = false;
    } ///< Always release the operation guard.
};

std::uint64_t next_identity();             ///< Nonwrapping schema/model owner identity allocator.
void validate_name(std::string_view name); ///< Bounded portable authoring identifier.
void validate_value(const value& data,
                    std::size_t max_text); ///< Type-independent finite/UTF-8 bounds.
void validate_utf8(std::string_view text); ///< Reject malformed UTF-8 and XML-forbidden characters.
} // namespace atlas::mvvm::detail
