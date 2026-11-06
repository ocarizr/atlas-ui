/** @file @brief Optional diagnostics cover UI binding publication and authored action routing. */
#include <array>
#include <atlas/ui_mvvm/action_handler.hpp>
#include <atlas/ui_mvvm/binding_session.hpp>
#include <gtest/gtest.h>
#include <sstream>

namespace atlas::ui_mvvm::tests
{
/// @brief New adapters share the existing compile-time profiling policy.
TEST(ui_bindings, CapturesPublicationAndActionDispatchWhenEnabled)
{
    auto profile = std::make_shared<diagnostics::profiler>();
    const diagnostics::services services{{}, profile};
    auto model =
        std::make_shared<mvvm::view_model>(mvvm::compile({"hud", {{"opacity", 0.5}}, {{"go"}}}));
    ui::element_tree tree;
    const auto target = tree.create();
    binding_session bindings{tree, model, 41, {}, {}, services};
    bindings.bind(target, {"opacity", ui::property::opacity});
    const std::array routes{action_binding_definition{ui::event_kind::pointer_up, "go"}};
    action_handler handler{model, routes, services};
    model->begin_frame(1);
    ui::routed_event event{};
    event.kind = ui::event_kind::pointer_up;
    event.phase = ui::route_phase::target;
    (void)handler.on_event(event, tree.commands());
    model->flush();
    tree.commands().begin_frame(1);
    bindings.publish();
    tree.apply(tree.commands().seal());
    std::ostringstream trace;
    profile->write_chrome_trace(trace);
    for (const auto name : {"ui.mvvm.publish", "ui.mvvm.actions.compile", "ui.mvvm.action"})
    {
        EXPECT_EQ(trace.str().find(name) != std::string::npos, diagnostics::instrumentation_enabled)
            << name;
    }

    EXPECT_EQ(profile->statistics().errors, 0);
}
} // namespace atlas::ui_mvvm::tests
