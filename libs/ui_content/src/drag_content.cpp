/** @file @brief Serializable drag policy with exact type matching and bounded UTF-8 offers. */
#include "drag_content.hpp"
#include <set>

namespace atlas::ui_content::detail
{
namespace
{
/// @brief Wire names map to the nonzero capability bits without accepting an empty mask.
void operations(fields& xml, ui::drag_operations& value)
{
    auto index = static_cast<unsigned>(value) - 1;
    xml.choice("operations", index, std::array{"copy", "move", "copy-move"});
    value = static_cast<ui::drag_operations>(index + 1);
}
} // namespace

/// @brief Round-trip static or dynamic source policy through exact XML attributes.
void drag_source_fields(fields& xml, drag_source_definition& data)
{
    xml.value("type", data.type);
    xml.value("text", data.text);
    xml.value("threshold", data.threshold);
    operations(xml, data.operations);
}

/// @brief Round-trip a bounded list of exact accepted type keys.
void drop_target_fields(fields& xml, ui::drop_target_options& data)
{
    std::string types;
    for (const auto& type : data.types)
    {
        if (!types.empty())
        {
            types += ';';
        }

        types += type;
    }

    xml.value("types", types, true);
    data.types.clear();
    std::size_t start = 0;
    while (start <= types.size())
    {
        const auto end = types.find(';', start);
        data.types.emplace_back(types.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos)
        {
            break;
        }

        start = end + 1;
    }

    operations(xml, data.operations);
}

/// @brief Reject malformed source or destination metadata before constructing a view.
void validate_drag(const element& node)
{
    const auto valid = [](ui::drag_operations value)
    {
        return value >= ui::drag_operations::copy && value <= ui::drag_operations::copy_move;
    };
    if (node.drag_source)
    {
        const auto& source = *node.drag_source;
        if (!std::isfinite(source.threshold) || source.threshold < 0 || source.threshold > 4096 ||
            !valid(source.operations))
        {
            throw std::invalid_argument("invalid authored drag source");
        }

        if (!source.type.empty())
        {
            (void)ui::drag_payload::text(source.type, source.text);
        }
        else if (!source.text.empty() || !node.script)
        {
            throw std::invalid_argument(
                "dynamic drag offer requires a behavior and no static text");
        }
    }

    if (node.drop_target)
    {
        const auto& target = *node.drop_target;
        if (!valid(target.operations) || target.types.empty() || target.types.size() > 32)
        {
            throw std::invalid_argument("invalid authored drop target");
        }

        std::set<std::string> unique;
        for (const auto& type : target.types)
        {
            (void)ui::drag_payload::binary(type, {});
            if (!unique.insert(type).second)
            {
                throw std::invalid_argument("duplicate drop type");
            }
        }
    }
}

/// @brief Wire authored policies onto private bound element candidates.
void configure_drag(ui::input_session& input, ui::element_handle target, const element& node)
{
    if (node.drag_source)
    {
        const auto& source = *node.drag_source;
        input.configure_drag_source(target, {source.type.empty()
                                                 ? ui::drag_payload{}
                                                 : ui::drag_payload::text(source.type, source.text),
                                             source.operations, source.threshold});
    }

    if (node.drop_target)
    {
        input.configure_drop_target(target, *node.drop_target);
    }
}
} // namespace atlas::ui_content::detail
