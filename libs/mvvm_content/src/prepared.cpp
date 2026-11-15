/** @file @brief Standalone ViewModel prepared document implementation. */
#include <atlas/mvvm_content/detail/prepared_definition.hpp>
#include <atlas/mvvm_content/detail/prepared_envelope.hpp>
#include <atlas/mvvm_content/prepared.hpp>

namespace atlas::mvvm_content
{
namespace
{
/// @brief Distinguish standalone ViewModel records from UI records.
constexpr detail::signature magic{std::byte{'A'}, std::byte{'V'}, std::byte{'M'}, std::byte{'P'}};
constexpr std::uint16_t wire_version = 1; ///< Prepared ViewModel schema v1.
} // namespace

/// @brief Validate the same authored schema as XML and generated factories.
std::vector<std::byte> encode_prepared(const mvvm::definition& source)
{
    (void)mvvm::compile(source);
    auto copy = source;
    detail::binary<false> archive;
    detail::prepared_definition(archive, copy);
    return detail::seal_prepared(archive, magic, wire_version);
}

/// @brief Publish only complete, validated definitions through normal MVVM compilation.
mvvm::schema decode_prepared(std::span<const std::byte> bytes)
{
    detail::binary<true> archive{detail::open_prepared(bytes, magic, wire_version)};
    mvvm::definition source;
    detail::prepared_definition(archive, source);
    if (!archive.exhausted())
    {
        throw std::invalid_argument("Trailing prepared ViewModel payload");
    }

    return mvvm::compile(source);
}
} // namespace atlas::mvvm_content
