/** @file @brief Owner-thread domain and model fixtures for authored Lua tests. */
#include "fixture.hpp"

namespace atlas::scripting::tests
{
/// @brief Both sides use identical typed schemas while retaining independent model values.
static const mvvm::schema& fixture_schema()
{
    static const auto schema =
        mvvm::compile({"lua_behavior_fixture",
                       {{"count", std::int64_t{0}}, {"mirror", std::int64_t{0}}},
                       {{"signal", {{"value", mvvm::value_type::integer}}}},
                       {{"changed", {{"label", mvvm::value_type::text}}}}});
    return schema;
}

/// @brief Keep all behavior execution under the same default limits as production.
fixture_domain::fixture_domain(const mvvm::schema& schema)
    : model(std::make_shared<mvvm::view_model>(schema))
{
    scripts.bind_context("state", model);
}

/// @brief Each LuaUnit case starts with fresh owner-local state and no issued requests.
fixture::fixture() : ui(fixture_schema()), application(fixture_schema())
{
}
} // namespace atlas::scripting::tests
