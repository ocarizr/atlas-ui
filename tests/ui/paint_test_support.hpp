/** @file @brief Shared paint fixtures with controlled geometry and isolated services. */
#pragma once
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui/paint_engine.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
class paint_fixture : public ::testing::Test
{
  protected:
    element_handle rectangle(element_handle parent = {}, paint_style style = {})
    {
        const auto node = tree.create(parent);
        style.fill = true;
        tree.set_paint(node, style);
        return node;
    } ///< Create one visible rectangle without game-specific semantics.

    paint_snapshot capture()
    {
        tree.set_viewport({0, 0, 100, 80});
        const auto source = tree.snapshot();
        return paint.calculate(source, layout.calculate(source));
    } ///< Resolve matching immutable inputs on the owning coordinator.

    jobs::job_system workers{2};                ///< Shutdown after all fixture consumers.
    element_tree tree;                          ///< Isolated registry/session.
    layout_engine layout{workers.dispatcher()}; ///< Sequential geometry reference.
    paint_engine paint{workers.dispatcher()};   ///< Default serial preparation.
}; ///< No mutable test data shared across cases.
} // namespace atlas::ui::tests
