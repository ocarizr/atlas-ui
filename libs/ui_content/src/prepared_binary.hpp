/** @file @brief Bounded little-endian scalar and sequence codecs for prepared UI. */
#pragma once
#include <algorithm>
#include <atlas/ui_content/prepared.hpp>
#include <bit>
#include <cmath>
#include <limits>

namespace atlas::ui_content::detail
{
/// @brief Maximum complete prepared view size, including its header.
inline constexpr std::size_t max_prepared_bytes = std::size_t{16} * 1024 * 1024;
/// @brief Admission cap for unique typed catalog references.
inline constexpr std::size_t max_prepared_dependencies = 32768;
/// @brief Admission cap for included templates and overrides.
inline constexpr std::size_t max_prepared_materials = 8192;

/// @brief Move bounded chunks into or out of an owned, endian-explicit payload.
template <bool Reading> class binary
{
  public:
    static constexpr bool reading = Reading; ///< Compile-time codec direction.

    explicit binary(std::span<const std::byte> bytes = {}) : remaining_{bytes}
    {
    } ///< Borrow input on decode; start empty on encode.

    /// @brief Keep the entire payload within the v1 byte budget.
    void raw(std::span<std::byte> bytes)
    {
        if constexpr (Reading)
        {
            if (bytes.size() > remaining_.size())
            {
                throw std::invalid_argument("Truncated prepared UI payload");
            }

            std::ranges::copy(remaining_.first(bytes.size()), bytes.begin());
            remaining_ = remaining_.subspan(bytes.size());
        }
        else
        {
            if (bytes.size() > max_prepared_bytes - output.size())
            {
                throw std::length_error("Prepared UI exceeds its byte budget");
            }

            output.insert(output.end(), bytes.begin(), bytes.end());
        }
    }

    /// @brief Encode supported scalars without persisting native object layout.
    template <class T> void field(T& value)
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            std::uint8_t byte = value ? 1 : 0;
            field(byte);
            if (byte > 1)
            {
                throw std::invalid_argument("Invalid prepared UI boolean");
            }

            value = byte != 0;
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            /// @brief Preserve IEEE binary32 and binary64 bits exactly.
            using bits = std::conditional_t<sizeof(T) == 4, std::uint32_t, std::uint64_t>;
            static_assert(std::numeric_limits<T>::is_iec559);
            auto encoded = std::bit_cast<bits>(value);
            field(encoded);
            value = std::bit_cast<T>(encoded);
            if (!std::isfinite(value))
            {
                throw std::invalid_argument("Nonfinite prepared UI number");
            }
        }
        else
        {
            integer(value);
        }
    }

    /// @brief Apply consecutive scalar fields in the documented wire order.
    template <class... T> void fields(T&... values)
    {
        (field(values), ...);
    }

    /// @brief Encode bounded UTF-8 text using a 32-bit byte count.
    void field(std::string& text)
    {
        const auto size = count(text.size(), 65536);
        if constexpr (Reading)
        {
            text.resize(size);
        }

        raw(std::as_writable_bytes(std::span{text}));
        mvvm::validate_text(text, 65536);
    }

    /// @brief Restrict byte-sized wire enums before casting into the authored vocabulary.
    template <class T> void choice(T& value, T last)
    {
        auto encoded = static_cast<std::uint8_t>(value);
        field(encoded);
        if (encoded > static_cast<std::uint8_t>(last))
        {
            throw std::invalid_argument("Unknown prepared UI enum value");
        }

        value = static_cast<T>(encoded);
    }

    /// @brief Encode ordered bounded records, checking their count before allocation.
    template <class T, class Codec>
    void sequence(std::vector<T>& items, std::size_t maximum, Codec codec)
    {
        const auto size = count(items.size(), maximum);
        if constexpr (Reading)
        {
            items.resize(size);
        }

        for (auto& item : items)
        {
            codec(*this, item);
        }
    }

    /// @brief Preserve the distinction between an absent record and explicit default values.
    template <class T, class Codec> void optional(std::optional<T>& item, Codec codec)
    {
        bool present = item.has_value();
        field(present);
        if (present)
        {
            if constexpr (Reading)
            {
                item.emplace();
            }

            if (item)
            {
                codec(*this, *item);
            }
        }
    }

    [[nodiscard]] bool exhausted() const noexcept
    {
        return remaining_.empty();
    } ///< Detect trailing data after the expected record.

    std::vector<std::byte> output; ///< Owned encoded bytes.

  private:
    /// @brief Bound string and record counts before narrowing or reserving memory.
    std::uint32_t count(std::size_t size, std::size_t maximum)
    {
        if (size > maximum)
        {
            throw std::length_error("Prepared UI collection exceeds its limit");
        }

        auto result = static_cast<std::uint32_t>(size);
        field(result);
        if (result > maximum || (Reading && result > remaining_.size()))
        {
            throw std::invalid_argument("Invalid prepared UI collection size");
        }

        return result;
    }

    /// @brief Use fixed-width byte order for arithmetic fields, including signed values.
    template <class T> void integer(T& value)
    {
        using unsigned_type = std::make_unsigned_t<T>; ///< Width-matched bits for shifts.
        auto bits = static_cast<std::uint64_t>(std::bit_cast<unsigned_type>(value));
        std::array<std::byte, sizeof(T)> bytes{};
        if constexpr (!Reading)
        {
            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                bytes.at(index) = static_cast<std::byte>((bits >> (index * 8)) & 255U);
            }
        }

        raw(bytes);
        if constexpr (Reading)
        {
            bits = 0;
            for (std::size_t index = 0; index < bytes.size(); ++index)
            {
                bits |= std::to_integer<std::uint64_t>(bytes.at(index)) << (index * 8);
            }

            value = std::bit_cast<T>(static_cast<unsigned_type>(bits));
        }
    }

    std::span<const std::byte> remaining_; ///< Unconsumed input, empty for encoders.
};

void validate_prepared(const prepared_view& source); ///< Verify the exact typed dependency closure.
} // namespace atlas::ui_content::detail
