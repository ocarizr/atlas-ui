/** @file @brief Small reusable builders for isolated sibling-domain facts. */
#pragma once
#include "layout_test_support.hpp"
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/element_tree.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <gtest/gtest.h>

namespace atlas::ui::tests
{
/// @brief Each fact owns its tree, persistent pool and injected layout client.
class boundary_test : public ::testing::Test
{
  protected:
    jobs::job_system pool{2};                ///< Outlives the injected client.
    element_tree tree;                       ///< Owner-affine authoring state.
    layout_engine engine{pool.dispatcher()}; ///< Shared pool, no native renderer.

    element_handle canvas()
    {
        const auto result = tree.create();
        tree.set_panel(result, canvas_layout{});
        return result;
    } ///< Create a canvas whose measure ignores relational child placement.

    void height(element_handle target, float value)
    {
        auto style = tree.layout(target);
        style.intrinsic.height = value;
        tree.set_layout(target, style);
    } ///< Keep scalar scenario setup separate from assertions.

    static layout_boundaries after(element_handle source)
    {
        return {{}, boundary_relation{source}};
    } ///< Default desired-size vertical relationship.
};
} // namespace atlas::ui::tests
