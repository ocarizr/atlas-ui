/** @file @brief Typed native actions stage demands and conditions cannot mutate model state. */
#include <array>
#include <atlas/scripting/callables.hpp>
#include <atlas/scripting/domain.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief Metadata-backed native functions bind once and preserve exact scalar argument types.
TEST(callable_registry, InvokesNativeActionWithDeferredModelWrites)
{
    const auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"hud", {{"xp", std::int64_t{0}}}}));
    domain scripts;
    scripts.bind_context("hud", model);
    scripts.register_action({"project", callable_origin_kind::project},
                            {.id = "project.gain",
                             .label = "Gain",
                             .category = "Progress",
                             .parameters = {{"amount", mvvm::value_type::integer}}},
                            [](action_context& context, std::span<const mvvm::value> arguments)
                            {
                                context.set("xp", std::get<std::int64_t>(context.get("xp")) +
                                                      std::get<std::int64_t>(arguments[0]));
                            });
    const auto binding = scripts.bind_callable("project.gain", "hud");
    model->begin_frame(1);
    scripts.begin_frame(1);
    const std::array<mvvm::value, 1> arguments{std::int64_t{5}};
    scripts.invoke_action(binding, arguments);
    EXPECT_EQ(std::get<std::int64_t>(model->read(model->description().field("xp"))), 0);
    model->flush();
    EXPECT_EQ(std::get<std::int64_t>(model->read(model->description().field("xp"))), 5);
    EXPECT_EQ(scripts.inspect().interpreters, 0U);
}
} // namespace atlas::scripting::tests
