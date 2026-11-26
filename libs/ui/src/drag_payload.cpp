/** @file @brief Immutable, bounded drag payload ownership independent of source buffers. */
#include <atlas/ui/drag_drop.hpp>
#include <stdexcept>

namespace atlas::ui
{
namespace
{
/// @brief Reject invalid Unicode scalars, overlong encodings and embedded terminators.
void validate_text(std::string_view text)
{
    std::uint32_t scalar = 0, minimum = 0;
    unsigned remaining = 0;
    for (const unsigned char byte : text)
    {
        if (!remaining)
        {
            if (byte > 0 && byte < 128)
            {
                continue;
            }

            if (byte >= 194 && byte <= 223)
            {
                remaining = 1;
                scalar = byte & 31;
                minimum = 128;
            }
            else if (byte >= 224 && byte <= 239)
            {
                remaining = 2;
                scalar = byte & 15;
                minimum = 2048;
            }
            else if (byte >= 240 && byte <= 244)
            {
                remaining = 3;
                scalar = byte & 7;
                minimum = 65536;
            }
            else
            {
                throw std::invalid_argument("drag text must be non-null UTF-8");
            }
        }
        else
        {
            if ((byte & 192) != 128)
            {
                throw std::invalid_argument("invalid drag UTF-8 continuation");
            }

            scalar = (scalar << 6) | (byte & 63);
            if (!--remaining &&
                (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)))
            {
                throw std::invalid_argument("invalid drag Unicode scalar");
            }
        }
    }

    if (remaining)
    {
        throw std::invalid_argument("truncated drag UTF-8");
    }
}
} // namespace

/// @brief The resource lease outlives the two PMR containers.
class drag_payload::data
{
  public:
    data(std::string_view key, std::span<const std::byte> value,
         std::shared_ptr<std::pmr::memory_resource> resource, bool text)
        : memory(std::move(resource)), type(key, memory.get()),
          bytes(value.begin(), value.end(), memory.get()), textual(text)
    {
    } ///< Construct once; published through a const shared owner only.

    std::shared_ptr<std::pmr::memory_resource> memory; ///< Retained allocation domain.
    std::pmr::string type;                             ///< Validated short type key.
    std::pmr::vector<std::byte> bytes;                 ///< Bounded copied data.
    bool textual; ///< Empty text remains distinguishable from binary.
};

/// @brief ASCII identifiers are portable and never contain paths or whitespace.
drag_payload drag_payload::binary(std::string_view type, std::span<const std::byte> bytes,
                                  std::shared_ptr<std::pmr::memory_resource> memory)
{
    if (type.empty() || type.size() > 64)
    {
        throw std::invalid_argument("invalid drag type length");
    }

    for (const char value : type)
    {
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '.' || value == '_' || value == '-' ||
              value == ':'))
        {
            throw std::invalid_argument("invalid drag type key");
        }
    }

    if (bytes.size() > 65536)
    {
        throw std::length_error("drag payload exceeds 65536 bytes");
    }

    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](auto*)
                  {
                  }};
    }

    drag_payload result;
    result.data_ = std::make_shared<data>(type, bytes, std::move(memory), false);
    return result;
}

/// @brief Text uses the same ownership and limits, plus strict UTF-8 validation.
drag_payload drag_payload::text(std::string_view type, std::string_view text,
                                std::shared_ptr<std::pmr::memory_resource> memory)
{
    if (text.size() > 65536)
    {
        throw std::length_error("drag payload exceeds 65536 bytes");
    }

    validate_text(text);
    auto result =
        binary(type, std::as_bytes(std::span{text.data(), text.size()}), std::move(memory));
    // This is the only mutable alias and exists before publication.
    const_cast<data&>(*result.data_).textual = true;
    return result;
}

/// @brief Report whether an immutable typed offer exists.
drag_payload::operator bool() const noexcept
{
    return bool(data_);
}

/// @brief Return the stable validated type key or an empty absent offer.
std::string_view drag_payload::type() const noexcept
{
    return data_ ? std::string_view(data_->type) : std::string_view{};
}

/// @brief Borrow immutable bytes while this shared payload handle remains alive.
std::span<const std::byte> drag_payload::bytes() const noexcept
{
    return data_ ? std::span<const std::byte>(data_->bytes) : std::span<const std::byte>{};
}

/// @brief Expose UTF-8 text only for explicitly textual payloads.
std::string_view drag_payload::text() const noexcept
{
    return is_text() && !data_->bytes.empty()
               ? std::string_view(reinterpret_cast<const char*>(data_->bytes.data()),
                                  data_->bytes.size())
               : std::string_view{};
}

/// @brief Distinguish a valid empty textual offer from binary data.
bool drag_payload::is_text() const noexcept
{
    return data_ && data_->textual;
}
} // namespace atlas::ui
