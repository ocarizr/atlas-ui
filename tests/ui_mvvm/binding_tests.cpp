/** @file @brief ViewModel source-of-truth updates become explicit UI command demands. */
#include <atlas/ui_mvvm/binding_session.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_mvvm::tests
{
/// @brief Only publication changes UI, while reverse changes remain model demands until flush.
TEST(ui_bindings, PublishesNamedFieldsAndSuppressesTwoWayEcho)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"hud", {{"opacity", 0.75}}}));
    ui::element_tree tree;
    const auto target = tree.create();
    binding_session bindings{tree, model, 41};
    const auto token =
        bindings.bind(target, {"opacity", ui::property::opacity, binding_mode::two_way});
    tree.commands().begin_frame(1);
    bindings.publish();
    tree.apply(tree.commands().seal());
    EXPECT_FLOAT_EQ(tree.requested(target).opacity, 0.75F);
    model->begin_frame(1);
    bindings.write_back(token, 0.5F);
    EXPECT_DOUBLE_EQ(model->number(model->description().field("opacity")), 0.75);
    model->flush();
    tree.commands().begin_frame(2);
    bindings.publish();
    tree.apply(tree.commands().seal());
    EXPECT_FLOAT_EQ(tree.requested(target).opacity, 0.5F);
    bindings.write_back(token, 0.5F);
    model->flush();
    tree.commands().begin_frame(3);
    bindings.publish();
    EXPECT_EQ(tree.commands().pending(), 0);
    tree.apply(tree.commands().seal());
}

/// @brief Retirement avoids stale generation writes and releases all model subscriptions.
TEST(ui_bindings, RetiresRemovedTargetsAndUnsubscribesOnDestruction)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"hud", {{"visible", true}}}));
    ui::element_tree tree;
    const auto target = tree.create();
    {
        binding_session bindings{tree, model, 41};
        const auto token = bindings.bind(target, {"visible", ui::property::visible});
        tree.remove(target);
        tree.commands().begin_frame(1);
        bindings.publish();
        EXPECT_EQ(tree.commands().pending(), 0);
        tree.apply(tree.commands().seal());
        EXPECT_THROW(bindings.write_back(token, false), std::invalid_argument);
    }
    model->begin_frame(1);
    model->set(model->description().field("visible"), false);
    EXPECT_NO_THROW(model->flush());
}

/// @brief Boolean bindings retain type and direction through both explicit sync boundaries.
TEST(ui_bindings, PublishesAndWritesBackVisibilityWithoutNumericCoercion)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"hud", {{"visible", false}}}));
    ui::element_tree tree;
    const auto target = tree.create();
    binding_session bindings{tree, model, 41};
    const auto token =
        bindings.bind(target, {"visible", ui::property::visible, binding_mode::two_way});
    tree.commands().begin_frame(1);
    bindings.publish();
    tree.apply(tree.commands().seal());
    EXPECT_FALSE(tree.requested(target).visible);
    model->begin_frame(1);
    bindings.write_back(token, true);
    model->flush();
    EXPECT_TRUE(model->boolean(model->description().field("visible")));
}
} // namespace atlas::ui_mvvm::tests
