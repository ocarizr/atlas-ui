/** @file @brief Shared bounded content validation and strict XML vocabulary helpers. */
#pragma once
#include <atlas/mvvm_content/document.hpp>
#include <pugixml.hpp>

namespace atlas::mvvm_content::detail
{
void check_limits(content_limits limits); ///< Reject unsupported processing budgets.
void check_definition(const mvvm::definition& source,
                      content_limits limits);       ///< Bound before copying metadata.
std::string_view type_name(mvvm::value_type type);  ///< Canonical XML/C++ scalar spelling.
mvvm::value_type parse_type(std::string_view name); ///< Reject unknown type vocabulary.
mvvm::value parse_value(mvvm::value_type type,
                        std::string_view text);  ///< Strict locale-independent conversion.
std::string value_text(const mvvm::value& data); ///< Lossless scalar text representation.
void attributes(
    pugi::xml_node node,
    std::initializer_list<std::string_view> allowed); ///< Reject unknown/duplicate attributes.
std::string required(pugi::xml_node node,
                     const char* name); ///< Required attribute, including empty text values.
void empty(pugi::xml_node node);        ///< Require no non-comment/non-whitespace children.
std::vector<pugi::xml_node>
children(pugi::xml_node node); ///< Return elements, rejecting DTD/PI/text content.
} // namespace atlas::mvvm_content::detail
