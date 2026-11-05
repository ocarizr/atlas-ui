/** @file @brief Data-authored UI events invoke ViewModel protocols without widget-specific logic.
 */
#include <array>
#include <atlas/ui_mvvm/action_handler.hpp>
#include <atlas/ui_mvvm/binding_session.hpp>
#include <gtest/gtest.h>

namespace atlas::ui_mvvm::tests
{
/// @brief Native/scripting behavior consumes a protocol; fields themselves remain data-defined.
class opacity_action final : public mvvm::observer
{
  public:
    mvvm::field_handle field; ///< Pre-resolved source slot.

    explicit opacity_action(mvvm::field_handle target) : field(target)
    {
    } ///< Cache once at wiring.

    void on_notify(const mvvm::notification& event, mvvm::view_model& model) override
    {
        model.set(field, event.arguments.front());
    } ///< Use the same deferred path as future Lua event dispatch.
};

/// @brief Target filtering, model flush and UI publication are distinct explicit boundaries.
TEST(ui_actions, AuthoredRoutesQueueActionsThenBindingsPublishCommittedValues)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile(
        {"hud", {{"opacity", 1.0}}, {{"set_opacity", {{"value", mvvm::value_type::number}}}}}));
    model->subscribe(model->description().action("set_opacity"),
                     std::make_shared<opacity_action>(model->description().field("opacity")));
    const std::array routes{
        action_binding_definition{ui::event_kind::pointer_up, "set_opacity", {mvvm::value{0.5}}}};
    action_handler handler{model, routes};
    ui::element_tree tree;
    const auto target = tree.create();
    binding_session bindings{tree, model, 41};
    bindings.bind(target, {"opacity", ui::property::opacity});
    ui::routed_event event{};
    event.kind = ui::event_kind::pointer_up;
    event.phase = ui::route_phase::bubble;
    model->begin_frame(1);
    (void)handler.on_event(event, tree.commands());
    EXPECT_EQ(model->inspect().commands, 0);
    event.phase = ui::route_phase::target;
    (void)handler.on_event(event, tree.commands());
    EXPECT_DOUBLE_EQ(model->number(model->description().field("opacity")), 1.0);
    model->flush();
    tree.commands().begin_frame(1);
    bindings.publish();
    tree.apply(tree.commands().seal());
    EXPECT_FLOAT_EQ(tree.requested(target).opacity, 0.5F);
}

/// @brief Bad signatures and semantic input filters fail during setup, before any event dispatch.
TEST(ui_actions, RejectsInvalidAuthoredProtocols)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"hud", {}, {{"go"}}}));
    const std::array bad{action_binding_definition{ui::event_kind::action, "go"}};
    EXPECT_THROW((action_handler{model, bad}), std::invalid_argument);
    const std::array arguments{
        action_binding_definition{ui::event_kind::pointer_up, "go", {mvvm::value{true}}}};
    EXPECT_THROW((action_handler{model, arguments}), std::invalid_argument);
}

/// @brief Semantic input identity and interaction phase must both match the authored route.
TEST(ui_actions, FiltersSemanticInputActionsAndPhasesBeforeInvocation)
{
    auto model = std::make_shared<mvvm::view_model>(mvvm::compile({"hud", {}, {{"go"}}}));
    const std::array routes{action_binding_definition{ui::event_kind::action,
                                                      "go",
                                                      {},
                                                      ui::route_phase::target,
                                                      17,
                                                      input::action_phase::performed}};
    action_handler handler{model, routes};
    ui::element_tree tree;
    model->begin_frame(1);
    ui::routed_event event{};
    event.kind = ui::event_kind::action;
    event.phase = ui::route_phase::target;
    (void)handler.on_event(event, tree.commands());
    input::action_event action{
        18, input::value_type::button, input::action_phase::performed, {1, 0}, 1};
    event.action = action;
    (void)handler.on_event(event, tree.commands());
    action.action = 17;
    action.phase = input::action_phase::completed;
    event.action = action;
    (void)handler.on_event(event, tree.commands());
    EXPECT_EQ(model->inspect().commands, 0);
    action.phase = input::action_phase::performed;
    event.action = action;
    (void)handler.on_event(event, tree.commands());
    EXPECT_EQ(model->inspect().commands, 1);
    model->flush();
}
} // namespace atlas::ui_mvvm::tests
