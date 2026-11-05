/** @file @brief Binding admission, conversion, cleanup and publication failure invariants. */
#include <atlas/ui_mvvm/binding_session.hpp>
#include <future>
#include <gtest/gtest.h>

namespace atlas::ui_mvvm::tests
{
/// @brief Each test owns its model, UI tree and subscription lifetime.
class binding_fixture : public ::testing::Test
{
  protected:
    /// @brief Isolated source-of-truth instance with representative scalar types.
    std::shared_ptr<mvvm::view_model> model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"hud",
                                                          {{"opacity", 0.5},
                                                           {"width", std::int64_t{20}},
                                                           {"visible", true},
                                                           {"text", std::string{"text"}}}}));
    ///< Independent source state.
    ui::element_tree tree;                     ///< Outlives the adapter.
    binding_session bindings{tree, model, 41}; ///< RAII releases all observer tokens first.
};

/// @brief Failed setup cannot consume a target/property slot or register an invalid protocol.
TEST_F(binding_fixture, RejectsUnknownFieldsTypesDuplicatePropertiesAndDirections)
{
    const auto target = tree.create();
    EXPECT_THROW(bindings.bind(target, {"missing", ui::property::opacity}), std::invalid_argument);
    EXPECT_THROW(bindings.bind(target, {"text", ui::property::width}), std::invalid_argument);
    const auto token = bindings.bind(target, {"opacity", ui::property::opacity});
    EXPECT_THROW(bindings.bind(target, {"opacity", ui::property::opacity}), std::invalid_argument);
    EXPECT_THROW(bindings.write_back(token, 0.1F), std::invalid_argument);
    bindings.unbind(token);
    EXPECT_NO_THROW(bindings.unbind(token));
    EXPECT_THROW(bindings.write_back(token, 0.1F), std::invalid_argument);
}

/// @brief A later invalid source value cannot leave earlier dirty UI commands partially admitted.
TEST_F(binding_fixture, PreflightsAllDirtyValuesBeforePublishingAnyCommands)
{
    const auto width = tree.create();
    const auto opacity = tree.create();
    bindings.bind(width, {"width", ui::property::width});
    bindings.bind(opacity, {"opacity", ui::property::opacity});
    model->begin_frame(1);
    model->set(model->description().field("opacity"), 2.0);
    model->flush();
    tree.commands().begin_frame(1);
    EXPECT_THROW(bindings.publish(), std::invalid_argument);
    EXPECT_EQ(tree.commands().pending(), 0);
    model->set(model->description().field("opacity"), 0.25);
    model->flush();
    bindings.publish();
    EXPECT_EQ(tree.commands().pending(), 2);
    tree.apply(tree.commands().seal());
    EXPECT_FLOAT_EQ(tree.requested(opacity).opacity, 0.25F);
}

/// @brief Reverse numeric bindings cannot truncate into an integer or erase automatic sizing.
TEST_F(binding_fixture, ReverseConversionsRejectLossAndPreserveTheSource)
{
    const auto target = tree.create();
    const auto token = bindings.bind(target, {"width", ui::property::width, binding_mode::two_way});
    model->begin_frame(1);
    EXPECT_THROW(bindings.write_back(token, 1.5F), std::invalid_argument);
    EXPECT_THROW(bindings.write_back(token, std::monostate{}), std::invalid_argument);
    EXPECT_EQ(model->inspect().commands, 0);
    EXPECT_EQ(model->integer(model->description().field("width")), 20);
}

/// @brief A reused UI slot never inherits bindings authored for an older generation.
TEST_F(binding_fixture, ReusedUiSlotsDoNotInheritRetiredBindings)
{
    const auto original = tree.create();
    bindings.bind(original, {"visible", ui::property::visible});
    tree.remove(original);
    const auto replacement = tree.create();
    EXPECT_NE(replacement, original);
    tree.commands().begin_frame(1);
    bindings.publish();
    EXPECT_EQ(tree.commands().pending(), 0);
    tree.apply(tree.commands().seal());
}

/// @brief Foreign worker access fails before touching mutable tree/model metadata.
TEST_F(binding_fixture, RejectsWorkerPublication)
{
    auto task = std::async(std::launch::async,
                           [&]
                           {
                               EXPECT_THROW(bindings.publish(), std::logic_error);
                           });
    task.get();
}

/// @brief Integers must survive the UI float representation exactly.
TEST(ui_bindings, RejectsIntegerPrecisionLossAtBindingSetup)
{
    auto model = std::make_shared<mvvm::view_model>(
        mvvm::compile({"hud", {{"width", std::int64_t{16777217}}}}));
    ui::element_tree tree;
    binding_session bindings{tree, model, 41};
    const auto target = tree.create();
    EXPECT_THROW(bindings.bind(target, {"width", ui::property::width}), std::invalid_argument);
}
} // namespace atlas::ui_mvvm::tests
