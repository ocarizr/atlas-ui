/** @file @brief Canonical editor serialization without filesystem ownership. */
#include "content_state.hpp"
#include <stdexcept>

namespace atlas::mvvm_content
{
namespace
{
/// @brief Bound output while pugixml writes rather than after an unbounded string grows.
class writer final : public pugi::xml_writer
{
  public:
    std::string text;    ///< Owned output bytes.
    std::size_t maximum; ///< Caller-validated byte bound.

    explicit writer(std::size_t limit) : maximum(limit)
    {
    } ///< No filesystem destination.

    void write(const void* data, std::size_t size) override
    {
        if (size > maximum - text.size())
        {
            throw std::overflow_error("MVVM XML output limit exhausted");
        }

        text.append(static_cast<const char*>(data), size);
    } ///< Enforce the output byte budget for every writer chunk.
};

/// @brief Preserve declared parameter order in both XML and generated factories.
void write_signals(pugi::xml_node parent, const char* tag,
                   const std::vector<mvvm::signal_definition>& signals)
{
    for (const auto& signal : signals)
    {
        auto node = parent.append_child(tag);
        node.append_attribute("name").set_value(signal.name.c_str());
        for (const auto& parameter : signal.parameters)
        {
            auto child = node.append_child("Param");
            child.append_attribute("name").set_value(parameter.name.c_str());
            const auto type = detail::type_name(parameter.type);
            child.append_attribute("type").set_value(type.data(), type.size());
        }
    }
}
} // namespace

/// @brief XML is an authoring contract, never an ABI dump of runtime objects.
std::string write_xml(const mvvm::definition& source, content_limits limits,
                      const diagnostics::services& destinations)
{
    const diagnostics::operation_scope scope{destinations, {"mvvm.xml.write", "mvvm.content"}};
    detail::check_definition(source, limits);
    pugi::xml_document document;
    auto declaration = document.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";
    auto root = document.append_child("ViewModel");
    root.append_attribute("version") = "1";
    root.append_attribute("name").set_value(source.name.c_str());
    auto fields = root.append_child("Fields");
    for (const auto& field : source.fields)
    {
        auto node = fields.append_child("Field");
        node.append_attribute("name").set_value(field.name.c_str());
        const auto type = detail::type_name(mvvm::type_of(field.initial));
        node.append_attribute("type").set_value(type.data(), type.size());
        node.append_attribute("value").set_value(detail::value_text(field.initial).c_str());
    }

    write_signals(root.append_child("Actions"), "Action", source.actions);
    write_signals(root.append_child("Events"), "Event", source.events);
    writer output{limits.max_bytes};
    document.save(output, "  ", pugi::format_default, pugi::encoding_utf8);
    return std::move(output.text);
}
} // namespace atlas::mvvm_content
