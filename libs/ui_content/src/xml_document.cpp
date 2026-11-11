/** @file @brief Parse and save versioned viewport UI without opening files. */
#include "document_validation.hpp"
#include "xml_fields.hpp"

namespace atlas::ui_content
{
namespace
{
/// @brief Accept only XML 1.0 character references and the five built-in entities.
bool valid_entity(std::string_view name)
{
    if (name == "amp" || name == "lt" || name == "gt" || name == "quot" || name == "apos")
    {
        return true;
    }

    if (!name.starts_with('#'))
    {
        return false;
    }

    name.remove_prefix(1);
    const bool hexadecimal = name.starts_with('x');
    if (hexadecimal)
    {
        name.remove_prefix(1);
    }

    std::uint32_t value{};
    const auto parsed =
        std::from_chars(name.data(), name.data() + name.size(), value, hexadecimal ? 16 : 10);
    return parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size() &&
           (value == 9 || value == 10 || value == 13 || (value >= 0x20 && value <= 0xD7FF) ||
            (value >= 0xE000 && value <= 0xFFFD) || (value >= 0x10000 && value <= 0x10FFFF));
}

/// @brief Reject invalid escapes before the parser can truncate a decoded attribute at NUL.
void validate_entities(std::string_view bytes)
{
    while (!bytes.empty())
    {
        const auto ampersand = bytes.find_first_of("<&");
        if (ampersand == bytes.npos)
        {
            return;
        }

        bytes.remove_prefix(ampersand);
        if (bytes.starts_with("<!--"))
        {
            const auto end = bytes.find("-->", 4);
            if (end == bytes.npos)
            {
                throw std::invalid_argument("Unterminated UI XML comment");
            }

            bytes.remove_prefix(end + 3);
        }
        else if (bytes.front() == '&')
        {
            const auto end = bytes.find(';', 1);
            if (end == bytes.npos || !valid_entity(bytes.substr(1, end - 1)))
            {
                throw std::invalid_argument("Invalid UI XML entity");
            }

            bytes.remove_prefix(end + 1);
        }
        else
        {
            bytes.remove_prefix(1);
        }
    }
}

/// @brief Identify the one supported root and reject processing instructions and DTDs.
pugi::xml_node root_of(const pugi::xml_document& xml)
{
    pugi::xml_node root;
    for (auto node : xml.children())
    {
        if (node.type() == pugi::node_comment ||
            (node.type() == pugi::node_declaration && node == xml.first_child()))
        {
            continue;
        }

        if (root || node.type() != pugi::node_element || std::string_view{node.name()} != "ui")
        {
            throw std::invalid_argument("Expected a single UI document root");
        }

        root = node;
    }

    if (!root)
    {
        throw std::invalid_argument("Missing UI document root");
    }

    return root;
}

/// @brief Encode or decode an independently named instance of a ViewModel definition.
void model_fields(detail::fields& xml, model_context& model)
{
    xml.value("name", model.name, true);
    xml.reference("asset", model.source, assets::asset_type::view_model);
}

/// @brief Enforce the version and per-document limits while converting the source.
document read_document(pugi::xml_node root)
{
    detail::fields header{root, true};
    std::string version;
    header.value("version", version, true);
    header.finish();
    if (version != "1")
    {
        throw std::invalid_argument("Unsupported UI XML version: " + version);
    }

    document result;
    for (auto node : root.children())
    {
        if (node.type() == pugi::node_comment)
        {
            continue;
        }

        try
        {
            const std::string_view name = node.name();
            if (node.type() == pugi::node_element && name == "model" &&
                result.models.size() < detail::max_models)
            {
                detail::fields xml{node, true};
                model_fields(xml, result.models.emplace_back());
                xml.finish();
                detail::expect_empty(node);
            }
            else if (node.type() == pugi::node_element && name == "element" &&
                     result.elements.size() < detail::max_elements)
            {
                detail::read_element(node, result.elements.emplace_back());
            }
            else
            {
                throw std::invalid_argument("Unexpected UI declaration or document limit exceeded");
            }
        }
        catch (const std::invalid_argument& error)
        {
            throw std::invalid_argument("UI declaration at byte " +
                                        std::to_string(node.offset_debug()) + ": " + error.what());
        }
    }

    detail::validate(result);
    return result;
}

/// @brief Stream canonical UTF-8 into a bounded output without touching persistent storage.
class bounded_writer final : public pugi::xml_writer
{
  public:
    /// @brief Refuse any write that would exceed the reader's own byte limit.
    void write(const void* data, std::size_t size) override
    {
        if (size > detail::max_document_bytes - bytes.size())
        {
            throw std::overflow_error("UI XML output limit exceeded");
        }

        bytes.append(static_cast<const char*>(data), size);
    }

    std::string bytes; ///< Owned canonical document output.
};
} // namespace

/// @brief Attach source identity to syntax and document-local validation failures.
document parse_xml(xml_source source, const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"ui.content.parse", "ui.content"}};
    try
    {
        mvvm::validate_text(source.bytes, detail::max_document_bytes);
        validate_entities(source.bytes);
        pugi::xml_document xml;
        const auto parsed =
            xml.load_buffer(source.bytes.data(), source.bytes.size(),
                            pugi::parse_full | pugi::parse_fragment, pugi::encoding_utf8);
        if (!parsed)
        {
            throw std::invalid_argument("XML byte " + std::to_string(parsed.offset) + ": " +
                                        parsed.description());
        }

        return read_document(root_of(xml));
    }
    catch (const std::invalid_argument& error)
    {
        throw std::invalid_argument(std::string{source.name} + ": " + error.what());
    }
    catch (const std::overflow_error& error)
    {
        throw std::invalid_argument(std::string{source.name} + ": " + error.what());
    }
}

/// @brief Save authored order and normalized numeric representations.
std::string write_xml(const document& source, const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"ui.content.save", "ui.content"}};
    detail::validate(source);
    pugi::xml_document xml;
    auto root = xml.append_child("ui");
    root.append_attribute("version").set_value("1");
    for (auto model : source.models)
    {
        detail::fields fields{root.append_child("model"), false};
        model_fields(fields, model);
    }

    for (const auto& element : source.elements)
    {
        detail::write_element(root.append_child("element"), element);
    }

    bounded_writer writer;
    xml.save(writer, "  ", pugi::format_default, pugi::encoding_utf8);
    return std::move(writer.bytes);
}
} // namespace atlas::ui_content
