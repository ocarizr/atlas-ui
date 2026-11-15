/**
 * @file
 * @brief Shared typed ViewModel wire fields for standalone and composed records.
 */
#pragma once
#include <atlas/mvvm/schema.hpp>
#include <type_traits>

namespace atlas::mvvm_content::detail
{
/// @brief Freeze collection bounds for this version of the wire definition.
inline constexpr mvvm::schema_limits prepared_schema_limits{4096, 1024, 32, 65536};

/// @brief Preserve the exact ViewModel scalar type and value in the prepared format.
inline constexpr auto prepared_scalar = [](auto& format, mvvm::value& value)
{
    auto tag = mvvm::type_of(value);
    format.choice(tag, mvvm::value_type::text);
    if constexpr (std::remove_reference_t<decltype(format)>::reading)
    {
        switch (tag)
        {
            case mvvm::value_type::boolean:
                value = false;
                break;
            case mvvm::value_type::integer:
                value = std::int64_t{};
                break;
            case mvvm::value_type::number:
                value = double{};
                break;
            case mvvm::value_type::text:
                value = std::string{};
                break;
        }
    }

    std::visit(
        [&](auto& field)
        {
            format.field(field);
        },
        value);
};

/// @brief Preserve schema order and typed action/event protocols without native handles.
template <class Archive> void prepared_definition(Archive& format, mvvm::definition& source)
{
    format.field(source.name);
    format.sequence(source.fields, prepared_schema_limits.max_fields,
                    [](auto& archive, mvvm::field_definition& field)
                    {
                        archive.field(field.name);
                        prepared_scalar(archive, field.initial);
                    });
    const auto signal = [](auto& archive, mvvm::signal_definition& value)
    {
        archive.field(value.name);
        archive.sequence(value.parameters, prepared_schema_limits.max_parameters,
                         [](auto& parameter_archive, mvvm::parameter_definition& parameter)
                         {
                             parameter_archive.field(parameter.name);
                             parameter_archive.choice(parameter.type, mvvm::value_type::text);
                         });
    };
    format.sequence(source.actions, prepared_schema_limits.max_signals, signal);
    format.sequence(source.events, prepared_schema_limits.max_signals, signal);
}
} // namespace atlas::mvvm_content::detail
