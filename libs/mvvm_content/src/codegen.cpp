/** @file @brief Generate native factories that share the validated runtime layout without XML
 * parsing. */
#include "content_state.hpp"
#include <bit>
#include <limits>
#include <stdexcept>

namespace atlas::mvvm_content
{
namespace
{
/// @brief Fixed three-digit octal escapes cannot absorb following digits or inject C++ source.
std::string literal(std::string_view text)
{
    std::string result = "\"";
    result.reserve(text.size() * 4 + 2);
    for (const unsigned char c : text)
    {
        result += '\\';
        result += static_cast<char>('0' + ((c >> 6U) & 7U));
        result += static_cast<char>('0' + ((c >> 3U) & 7U));
        result += static_cast<char>('0' + (c & 7U));
    }

    result += '\"';
    return result;
}

/// @brief Preserve double bits and signed integer extremes in portable C++20 source.
std::string constant(const mvvm::value& value)
{
    if (const auto* text = std::get_if<std::string>(&value))
    {
        constexpr std::size_t chunk_bytes = 4096;
        if (text->size() <= chunk_bytes)
        {
            return "std::string{" + literal(*text) + "}";
        }

        // Separate append calls avoid both per-token and concatenated-literal compiler limits.
        std::string result =
            "[] { std::string text; text.reserve(" + std::to_string(text->size()) + ");\n";
        for (std::size_t offset = 0; offset < text->size(); offset += chunk_bytes)
        {
            const auto chunk = std::string_view{*text}.substr(offset, chunk_bytes);
            result +=
                "text.append(" + literal(chunk) + ", " + std::to_string(chunk.size()) + ");\n";
        }

        return result + "return text; }()";
    }

    if (const auto* boolean = std::get_if<bool>(&value))
    {
        return *boolean ? "true" : "false";
    }

    if (const auto* integer = std::get_if<std::int64_t>(&value))
    {
        if (*integer == std::numeric_limits<std::int64_t>::min())
        {
            return "std::numeric_limits<std::int64_t>::min()";
        }

        return "std::int64_t{" + std::to_string(*integer) + "LL}";
    }

    return "std::bit_cast<double>(std::uint64_t{" +
           std::to_string(std::bit_cast<std::uint64_t>(std::get<double>(value))) + "ULL})";
}

/// @brief A constrained make_ prefix avoids keywords and reserved C++ identifiers.
void function_identifier(std::string_view name)
{
    if (!name.starts_with("make_") || name.size() <= 5 || name.size() > 128 ||
        name.find("__") != std::string_view::npos)
    {
        throw std::invalid_argument("MVVM factory name must be a nonreserved make_ identifier");
    }

    for (const unsigned char c : name)
    {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '_'))
        {
            throw std::invalid_argument("invalid MVVM factory identifier");
        }
    }
}
} // namespace

/// @brief Native code constructs the same schema once and typed instance columns on each call.
std::string generate_factory_cpp(const mvvm::definition& source, std::string_view function_name,
                                 content_limits limits, const diagnostics::services& destinations)
{
    const diagnostics::operation_scope scope{destinations, {"mvvm.codegen", "mvvm.content"}};
    detail::check_definition(source, limits);
    function_identifier(function_name);
    std::string output = "#include <atlas/mvvm/view_model.hpp>\n#include <bit>\n#include "
                         "<limits>\n#include <utility>\nnamespace atlas::generated {\n";
    const auto append = [&](std::string_view text)
    {
        if (output.size() > limits.max_bytes * 8 ||
            text.size() > limits.max_bytes * 8 - output.size())
        {
            throw std::overflow_error("MVVM generated source limit exhausted");
        }

        output += text;
    };
    append("std::unique_ptr<mvvm::view_model> ");
    append(function_name);
    append("(mvvm::runtime_limits limits, std::shared_ptr<std::pmr::memory_resource> memory, "
           "const diagnostics::services& destinations) {\n");
    append("static const auto description = [] { mvvm::definition data; data.name = ");
    append(literal(source.name));
    append(";\n");
    for (const auto& field : source.fields)
    {
        append("data.fields.push_back({");
        append(literal(field.name));
        append(", mvvm::value{");
        append(constant(field.initial));
        append("}});\n");
    }

    for (const auto pair :
         {std::pair{"actions", &source.actions}, std::pair{"events", &source.events}})
    {
        for (const auto& signal : *pair.second)
        {
            append("data.");
            append(pair.first);
            append(".push_back({");
            append(literal(signal.name));
            append(", {");
            for (const auto& parameter : signal.parameters)
            {
                append("{");
                append(literal(parameter.name));
                append(", mvvm::value_type::");
                append(detail::type_name(parameter.type));
                append("},");
            }

            append("}});\n");
        }
    }

    append("return mvvm::compile(data, mvvm::schema_limits{" +
           std::to_string(limits.schema.max_fields) + "," +
           std::to_string(limits.schema.max_signals) + "," +
           std::to_string(limits.schema.max_parameters) + "," +
           std::to_string(limits.schema.max_text_bytes) +
           "}); }();\nreturn std::make_unique<mvvm::view_model>(description, limits, "
           "std::move(memory), destinations);\n}\n}\n");
    return output;
}
} // namespace atlas::mvvm_content
