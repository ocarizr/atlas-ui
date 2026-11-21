/** @file @brief Explicit isolated Flow preview preserves the last valid running snapshot. */
#include "fixture.hpp"
#include <atlas/editor_flow/preview.hpp>

namespace atlas::editor_flow::tests
{
/// @brief Pending/failing candidates use deterministic owner gates rather than sleeps.
class preview_test : public testing::Test
{
  protected:
    /// @brief One immediately ready or controlled failure with no application file access.
    class ready_load final : public ui_content::flow_load
    {
      public:
        /// @brief Borrow only owner-controlled fixture state.
        ready_load(streaming::catalog_streamer& storage, const bool& failed)
            : storage_{storage}, failed_{failed}
        {
        }

        std::unique_ptr<ui_content::instance> poll() override
        {
            if (failed_)
            {
                throw std::runtime_error("controlled preview load failure");
            }

            return ui_content::instance::build(
                ui_content::parse_xml({R"(<ui version="2" kind="menu"><element id="root"/></ui>)"}),
                {storage_});
        } ///< Compose only after the owner polls readiness.
      private:
        streaming::catalog_streamer& storage_; ///< Fixture-owned empty mount.
        const bool& failed_;                   ///< Owner gate.
    };

    jobs::job_system jobs{1}; ///< Shared infrastructure outlives preview/storage.
    streaming::catalog_streamer storage{
        jobs,
        assets::catalog::compile({example().graph.screens.front().asset.id.space}),
        {}};             ///< Empty fixture mount.
    bool failed = false; ///< Explicit controlled failure.

    /// @brief Independent loads retain no UI state from an old preview.
    ui_content::flow_loader loader()
    {
        return [this](const ui_content::flow_load_request&)
        {
            return std::make_unique<ready_load>(storage, failed);
        };
    }
};

/// @brief Dirty edits and failed replacement loads cannot retire the last valid preview.
TEST_F(preview_test, AppliesOnlyExplicitSnapshotsAndRetainsRunningPreviewAfterFailedReplacement)
{
    session source{example()};
    preview runtime{jobs.dispatcher()};
    runtime.apply(source.compile(), loader());
    for (int i = 0; i < 4 && !runtime.running(); ++i)
    {
        (void)runtime.frame({0, 0, 100, 100});
    }

    ASSERT_TRUE(runtime.running()) << runtime.diagnostic();
    EXPECT_EQ(runtime.presentation().front().screen, "home");
    auto edited = example();
    edited.graph.screens.front().id = "new";
    edited.graph.regions.front().initial = "new";
    edited.selection = {};
    edited.layout.clear();
    source.apply(edited);
    EXPECT_EQ(runtime.presentation().front().screen, "home");
    failed = true;
    runtime.apply(source.compile(), loader());
    (void)runtime.frame({0, 0, 100, 100});
    EXPECT_TRUE(runtime.running());
    EXPECT_EQ(runtime.presentation().front().screen, "home");
    EXPECT_NE(runtime.diagnostic().find("controlled"), std::string_view::npos);
    runtime.stop();
    EXPECT_FALSE(runtime.running());
}

/// @brief Cancelled private initialization does not install any UI or Lua instance.
TEST_F(preview_test, CancelledApplyNeverReplacesPublishedState)
{
    session source{example()};
    preview runtime{jobs.dispatcher()};
    runtime.apply(source.compile(), loader());
    runtime.cancel();
    (void)runtime.frame({0, 0, 100, 100});
    EXPECT_FALSE(runtime.running());
}

/// @brief Registration/native callbacks cannot mutate the coordinator during private preflight.
TEST_F(preview_test, RejectsReentrantNavigationFromRegistrationCallbacks)
{
    session source{example()};
    preview runtime{jobs.dispatcher()};
    runtime.apply(source.compile(), loader(),
                  [&](auto&)
                  {
                      EXPECT_THROW((void)runtime.request("open"), std::logic_error);
                  });
    for (int i = 0; i < 4 && !runtime.running(); ++i)
    {
        (void)runtime.frame({0, 0, 100, 100});
    }

    EXPECT_TRUE(runtime.running()) << runtime.diagnostic();
}
} // namespace atlas::editor_flow::tests
