/** @file @brief Compile editor/generated definitions into immutable typed slot metadata. */
#include "schema_state.hpp"
#include <algorithm>

namespace atlas::mvvm
{
namespace detail
{
/// @brief Only completed schema compilation can publish private immutable metadata.
struct schema_builder
{
    static schema publish(std::shared_ptr<const schema_data> data)
    {
        return schema{std::move(data)};
    } ///< Construct only after the full definition and indexes have been validated.
};
} // namespace detail

namespace
{
/// @brief Build deterministic lookup tables without changing authored declaration order.
template <class T> auto names(const std::vector<T>& values)
{
    std::vector<std::pair<std::string, std::uint32_t>> result;
    result.reserve(values.size());
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        detail::validate_name(values[i].name);
        result.emplace_back(values[i].name, static_cast<std::uint32_t>(i));
    }

    std::sort(result.begin(), result.end());
    if (std::adjacent_find(result.begin(), result.end(),
                           [](const auto& a, const auto& b)
                           {
                               return a.first == b.first;
                           }) != result.end())
    {
        throw std::invalid_argument("duplicate MVVM name");
    }

    return result;
}

/// @brief Signal signatures are bounded and validated before copying a schema.
std::size_t validate_signals(const std::vector<signal_definition>& signals, schema_limits limits)
{
    if (signals.size() > limits.max_signals)
    {
        throw std::overflow_error("MVVM signal limit exhausted");
    }

    std::size_t maximum = 0;
    for (const auto& signal : signals)
    {
        if (signal.parameters.size() > limits.max_parameters)
        {
            throw std::overflow_error("MVVM parameter limit exhausted");
        }

        maximum = std::max(maximum, signal.parameters.size());
        (void)names(signal.parameters);
        for (const auto& parameter : signal.parameters)
        {
            if (parameter.type > value_type::text)
            {
                throw std::invalid_argument("invalid MVVM parameter type");
            }
        }
    }

    return maximum;
}

/// @brief Binary search is used only while resolving authoring names to runtime slots.
std::uint32_t lookup(const std::vector<std::pair<std::string, std::uint32_t>>& index,
                     std::string_view name)
{
    const auto it = std::lower_bound(index.begin(), index.end(), name,
                                     [](const auto& item, auto key)
                                     {
                                         return item.first < key;
                                     });
    if (it == index.end() || it->first != name)
    {
        throw std::invalid_argument("unknown MVVM name: " + std::string{name});
    }

    return it->second;
}
} // namespace

/// @brief A schema copy shares immutable metadata and process-local slot identity.
schema::schema(std::shared_ptr<const detail::schema_data> data) : data_(std::move(data))
{
}

/// @brief Compilation is independent of UI, files and a concrete application class.
schema compile(const definition& source, schema_limits limits,
               const diagnostics::services& destinations)
{
    const diagnostics::operation_scope scope{destinations, {"mvvm.compile", "mvvm.cpu"}};
    if (!limits.max_fields || limits.max_fields > 65536 || !limits.max_signals ||
        limits.max_signals > 4096 || !limits.max_parameters || limits.max_parameters > 256 ||
        !limits.max_text_bytes || limits.max_text_bytes > 1048576)
    {
        throw std::invalid_argument("invalid MVVM schema limits");
    }

    detail::validate_name(source.name);
    if (source.fields.size() > limits.max_fields)
    {
        throw std::overflow_error("MVVM field limit exhausted");
    }

    const auto maximum_arguments =
        std::max(validate_signals(source.actions, limits), validate_signals(source.events, limits));
    auto fields = names(source.fields);
    auto actions = names(source.actions);
    auto events = names(source.events);
    for (const auto& field : source.fields)
    {
        detail::validate_value(field.initial, limits.max_text_bytes);
    }

    auto data = std::make_shared<detail::schema_data>();
    data->source = source;
    data->limits = limits;
    data->max_arguments = maximum_arguments;
    data->id = detail::next_identity();
    data->fields = std::move(fields);
    data->actions = std::move(actions);
    data->events = std::move(events);
    data->offsets.reserve(source.fields.size());
    for (const auto& field : source.fields)
    {
        data->offsets.push_back(static_cast<std::uint32_t>(
            data->counts[static_cast<std::size_t>(type_of(field.initial))]++));
    }

    return detail::schema_builder::publish(std::move(data));
}

/// @brief Repeated reads use the resolved handle rather than repeating this lookup.
field_handle schema::field(std::string_view name) const
{
    return {data_->id, lookup(data_->fields, name)};
}

/// @brief Actions and events have distinct namespaces and handle types.
action_handle schema::action(std::string_view name) const
{
    return {data_->id, lookup(data_->actions, name)};
}

/// @brief Resolve an authored output event once during wiring.
event_handle schema::event(std::string_view name) const
{
    return {data_->id, lookup(data_->events, name)};
}

/// @brief Forged indexes cannot escape schema identity/range validation.
value_type schema::type(field_handle slot) const
{
    if (slot.schema != data_->id || slot.index >= data_->source.fields.size())
    {
        throw std::invalid_argument("foreign or invalid MVVM field slot");
    }

    return type_of(data_->source.fields[slot.index].initial);
}

/// @brief Definition storage remains alive with this immutable schema.
const definition& schema::describe() const noexcept
{
    return data_->source;
}

/// @brief Runtime identity is intentionally independent of persistent asset identity.
std::uint64_t schema::identity() const noexcept
{
    return data_->id;
}
} // namespace atlas::mvvm
