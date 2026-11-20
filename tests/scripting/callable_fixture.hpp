/** @file @brief Independent registered-callable models, sources and frame ownership per test. */
#pragma once
#include <array>
#include <atlas/scripting/domain.hpp>
#include <gtest/gtest.h>

namespace atlas::scripting::tests
{
/// @brief Reuse tiny deterministic schemas without shared mutable script/model state.
class callable_test : public testing::Test
{
  protected:
    /// @brief Bind before admitting the first owner frame; module discovery needs no frame.
    void SetUp() override
    {
        scripts.bind_context("hud", model);
    }

    /// @brief Flush previous producer demands before moving both owners to the next frame.
    void begin()
    {
        if (frame)
        {
            model->flush();
        }

        model->begin_frame(++frame);
        scripts.begin_frame(frame);
    }

    /// @brief Publish a root revision and atomically rediscover its action/condition schema.
    void load(std::string text)
    {
        scripts.register_source({"entry", ++revision, "@project/actions.lua", std::move(text)});
        scripts.load_callable_module({"entry", {"project", callable_origin_kind::project}});
    }

    std::shared_ptr<mvvm::view_model> model = std::make_shared<mvvm::view_model>(mvvm::compile(
        {"hud",
         {{"xp", std::int64_t{0}}, {"enabled", true}},
         {{"gain", {{"amount", mvvm::value_type::integer}}}}})); ///< Per-test source of truth.
    domain scripts;                        ///< Retired before its bound model.
    std::uint64_t frame = 0, revision = 0; ///< Explicit monotonic owners.
};
} // namespace atlas::scripting::tests
