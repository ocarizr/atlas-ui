/**
 * @file
 * @brief Standalone definition preparation for the reusable ViewModel runtime.
 */
#pragma once
#include <atlas/mvvm/schema.hpp>
#include <cstddef>

namespace atlas::mvvm_content
{
/// @brief Validate and write a bounded prepared ViewModel definition.
[[nodiscard]] std::vector<std::byte> encode_prepared(const mvvm::definition& source);
/// @brief Decode and compile immutable ViewModel schema metadata without XML parsing.
[[nodiscard]] mvvm::schema decode_prepared(std::span<const std::byte> bytes);
} // namespace atlas::mvvm_content
