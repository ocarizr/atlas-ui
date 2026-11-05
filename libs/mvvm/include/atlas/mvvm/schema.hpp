/**
 * @file
 * @brief Editable ViewModel definitions and immutable validated runtime schemas. */
#pragma once
#include <atlas/diagnostics/instrumentation.hpp>
#include <cstdint>
#include <memory_resource>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace atlas::mvvm
{
/// @brief Closed scalar vocabulary with explicit serialization and binding conversions.
enum class value_type : std::uint8_t
{
    boolean, ///< Logical value.
    integer, ///< Signed 64-bit integer.
    number,  ///< Finite IEEE double.
    text     ///< Owned UTF-8 text.
};
using value = std::variant<bool, std::int64_t, double, std::string>; ///< Owned boundary value.
void validate_text(std::string_view text, std::size_t max_bytes);    ///< Checked UTF-8/XML text.
value_type type_of(const value& data) noexcept; ///< Stable variant-to-schema type mapping.

/// @brief Editor-owned field description; changes require compiling a new schema.
struct field_definition
{
    std::string name; ///< Unique field name, resolved once at binding setup.
    value initial;    ///< Typed default; no native class declaration is required.
};

/// @brief Named typed message argument.
struct parameter_definition
{
    std::string name; ///< Unique within this message.
    value_type type;  ///< Required argument type; no implicit conversions.
};

/// @brief Action or event protocol; behavior is supplied by observers or future scripting.
struct signal_definition
{
    std::string name;                               ///< Unique in its action/event namespace.
    std::vector<parameter_definition> parameters{}; ///< Ordered arguments.
};

/// @brief Serializable editor data, independent of widgets, renderer and native types.
struct definition
{
    std::string name;                         ///< ViewModel definition identity/name.
    std::vector<field_definition> fields{};   ///< Editable typed default values.
    std::vector<signal_definition> actions{}; ///< Invocable input protocol.
    std::vector<signal_definition> events{};  ///< Explicit output protocol.
};

/// @brief Work and content limits applied before publishing immutable metadata.
struct schema_limits
{
    std::size_t max_fields = 4096;      ///< At most 65536 fields.
    std::size_t max_signals = 1024;     ///< At most 4096 in each signal namespace.
    std::size_t max_parameters = 32;    ///< At most 256 parameters per message.
    std::size_t max_text_bytes = 65536; ///< At most 1048576 per text value.
};

/// @brief Checked slot identity shared by instances of one immutable schema.
struct field_handle
{
    std::uint64_t schema = 0; ///< In-process schema generation, never a serialized asset ID.
    std::uint32_t index = 0;  ///< Definition-order field index.
    bool operator==(const field_handle&) const = default; ///< Compare full slot identity.
};

/// @brief Action namespace slot; cannot be passed as a field/event handle accidentally.
struct action_handle
{
    std::uint64_t schema = 0; ///< Schema generation.
    std::uint32_t index = 0;  ///< Action index.
};

/// @brief Event namespace slot.
struct event_handle
{
    std::uint64_t schema = 0; ///< Schema generation.
    std::uint32_t index = 0;  ///< Event index.
};

namespace detail
{
/// @brief Private immutable schema indexes and offsets.
struct schema_data;
/// @brief Private validated publication helper.
struct schema_builder;
} // namespace detail

/// @brief Shareable immutable compiled metadata; name resolution is outside the update path.
class schema
{
  public:
    schema(const schema&) = default; ///< Share immutable metadata, including rvalue arguments.
    schema& operator=(const schema&) = default; ///< Retain validated identity through value copies.
    field_handle field(std::string_view name) const;   ///< Checked named field resolution.
    action_handle action(std::string_view name) const; ///< Checked named action resolution.
    event_handle event(std::string_view name) const;   ///< Checked named event resolution.
    void
    validate(action_handle slot,
             std::span<const value> arguments) const; ///< Validate an immutable action protocol.
    void
    validate(event_handle slot,
             std::span<const value> arguments) const; ///< Validate an immutable event protocol.
    value_type type(field_handle slot) const;         ///< Reject foreign/stale/forged handles.
    const definition& describe() const noexcept;      ///< Borrow immutable authoring metadata.
    std::uint64_t identity() const noexcept;          ///< Process-local schema generation.
  private:
    friend struct detail::schema_builder; ///< Validated schema publication only.
    friend class view_model;              ///< Instances share validated typed offsets.
    explicit schema(std::shared_ptr<const detail::schema_data> data); ///< Publish validated data.
    std::shared_ptr<const detail::schema_data> data_; ///< Immutable schema lifetime.
};

/// @brief Copy and validate authored/generated definitions into immutable metadata.
schema compile(const definition& source, schema_limits limits = {},
               const diagnostics::services& destinations = {});
///< Validate and copy editor/generated data into one immutable runtime schema.
} // namespace atlas::mvvm
