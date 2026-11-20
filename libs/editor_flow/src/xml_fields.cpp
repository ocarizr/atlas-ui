/** @file @brief Strict XML fields, item references and typed scalar values. */
#include "xml_fields.hpp"

namespace atlas::editor_flow::detail
{
/// @brief Optional policy flags preserve the difference between inheritance and an override.
void fields::optional_bool(const char* name, std::optional<bool>& data)
{
    known_.insert(name);
    if (reading_ && node_.attribute(name))
    {
        data = false;
    }

    if (data)
    {
        value(name, *data);
    }
}

/// @brief Unknown attributes and duplicate spellings are rejected rather than silently discarded.
void fields::finish() const
{
    if (!reading_)
    {
        return;
    }

    std::set<std::string_view> seen;
    for (const auto attr : node_.attributes())
    {
        if (!known_.contains(attr.name()) || !seen.insert(attr.name()).second)
        {
            throw std::invalid_argument("Unknown or duplicate Flow draft attribute");
        }
    }
}

/// @brief Whitespace is skipped by parsing; other non-comment children are rejected.
void empty(pugi::xml_node node)
{
    for (const auto child : node.children())
    {
        if (child.type() != pugi::node_comment)
        {
            throw std::invalid_argument("Unexpected Flow draft content");
        }
    }
}

/// @brief Reject oversize output before appending another XML chunk.
void writer::write(const void* data, std::size_t size)
{
    if (size > max_bytes - bytes.size())
    {
        throw std::length_error("Flow draft XML byte budget exhausted");
    }

    bytes.append(static_cast<const char*>(data), size);
}

/// @brief No text-to-number, integer-to-double or truthy-value coercion crosses the schema.
void scalar_fields(fields& codec, mvvm::value& value)
{
    auto type = static_cast<unsigned>(mvvm::type_of(value));
    codec.value("type", type);
    if (type > 3)
    {
        throw std::invalid_argument("Invalid Flow draft scalar type");
    }

    if (codec.reading())
    {
        switch (static_cast<mvvm::value_type>(type))
        {
            case mvvm::value_type::boolean:
                value = false;
                break;
            case mvvm::value_type::integer:
                value = std::int64_t{0};
                break;
            case mvvm::value_type::number:
                value = 0.0;
                break;
            case mvvm::value_type::text:
                value = std::string{};
                break;
        }
    }

    std::visit(
        [&](auto& item)
        {
            codec.value("value", item);
        },
        value);
}

/// @brief Numeric kinds are versioned authoring vocabulary, distinct from runtime asset kinds.
void reference_fields(fields& codec, item_ref& item)
{
    auto kind = static_cast<unsigned>(item.kind);
    codec.value("kind", kind);
    if (kind > static_cast<unsigned>(item_kind::transition))
    {
        throw std::invalid_argument("Invalid Flow draft item kind");
    }

    item.kind = static_cast<item_kind>(kind);
    codec.value("id", item.id);
}
} // namespace atlas::editor_flow::detail
