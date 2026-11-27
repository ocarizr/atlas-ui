/**
 * @file
 * @brief Interactive Narrator acceptance over real UI virtual-list layout and owner routing.
 */
#include <atlas/accessibility_host/adapter.hpp>
#include <atlas/jobs/job_system.hpp>
#include <atlas/ui/accessibility_frame.hpp>
#include <atlas/ui/layout_engine.hpp>
#include <atlas/ui/virtual_list.hpp>
#include <cstdio>
#include <iostream>

namespace
{
/// @brief Share one dispatcher and the production list/input/layout implementation.
class virtual_walkthrough
{
  public:
    /// @brief Admit a ten-thousand-row source while retaining only the visible range and overscan.
    virtual_walkthrough()
    {
        viewport_ = tree_.create();
        tree_.set_panel(viewport_, atlas::ui::scroll_layout{});
        tree_.set_viewport({0, 0, 500, 80});
        list_ = std::make_unique<atlas::ui::virtual_list>(tree_, input_, viewport_,
                                                          workers_.dispatcher(),
                                                          [](atlas::ui::row_context&)
                                                          {
                                                          });
        input_.configure_accessibility(
            viewport_,
            {.role = atlas::accessibility::role::scroll_container, .name = "Virtual inventory"});
        list_->request_focus(1);
    }

    /// @brief Run manually; the native reader consumes the same immutable owner-frame bridge.
    int run(const atlas::diagnostics::services& sinks)
    {
        if (!atlas::accessibility_host::available())
        {
            std::cerr << "A native accessibility backend is required.\n";
            return 2;
        }

        atlas::window::window window{{"Atlas Virtual List Walkthrough", 720, 540}};
        const auto channel =
            std::make_shared<atlas::accessibility::channel>(atlas::accessibility::limits{}, sinks);
        atlas::accessibility_host::adapter host{window, channel, sinks};
        bool closed = false;
        while (!window.poll_events().close_requested && !closed)
        {
            const auto input = window.take_input();
            for (const auto& event : input.values())
            {
                if (event.kind == atlas::input::event_kind::control_changed &&
                    event.device.kind == atlas::input::device_kind::keyboard && event.value.x == 0)
                {
                    closed = key(event.control) || closed;
                }
            }

            publish(*channel, host.coordinates());
            host.notify();
            window.wait_for_events(16);
        }

        channel->close();
        host.notify();
        list_->detach();
        input_.shutdown(++frame_);
        tree_.apply(tree_.commands().seal());
        return 0;
    }

  private:
    /// @brief Realization occurs while input is idle; requests execute only in the input phase.
    void publish(atlas::accessibility::channel& channel,
                 atlas::accessibility_host::screen_transform coordinates)
    {
        list_->update(data_, 80);
        for (const auto& row : list_->rows())
        {
            input_.configure_accessibility(
                row.ticket.root, {.role = atlas::accessibility::role::list_item,
                                  .name = "Virtual row " + std::to_string(row.index + 1)});
        }

        const auto source = tree_.snapshot();
        input_.begin_frame(++frame_, source, layout_.calculate(source));
        list_->restore_focus();
        atlas::ui::accessibility_frame frame{
            channel, {coordinates.x, coordinates.y, coordinates.scale}, focus_owner_};
        frame.route(input_);
        input_.process({});
        frame.capture(input_);
        input_.tick();
        input_.late_tick();
        input_.end_frame();
        tree_.apply(tree_.commands().seal());
        frame.publish();
        focus_owner_ = frame.focus_owner();
    }

    /// @brief Logical navigation can materialize an unrealized row without UIA VirtualizedItem.
    bool key(std::uint32_t control)
    {
        using atlas::input::key;
        switch (static_cast<key>(control))
        {
            case key::home:
                list_->request_focus(1);
                break;
            case key::end:
                list_->request_focus(10000);
                break;
            case key::down:
                list_->request_focus(
                    std::min<atlas::ui::item_key>(10000, list_->focused_item() + 1));
                break;
            case key::up:
                list_->request_focus(std::max<atlas::ui::item_key>(2, list_->focused_item()) - 1);
                break;
            case key::escape:
                return true;
            default:
                break;
        }

        return false;
    }

    atlas::jobs::job_system workers_{1};    ///< One application-owned scheduler, joined last.
    atlas::ui::element_tree tree_;          ///< Real physical row identities and scroll commands.
    atlas::ui::input_session input_{tree_}; ///< Owner-phase focus/reveal and semantic requests.
    atlas::ui::layout_engine layout_{workers_.dispatcher()}; ///< Production clipped geometry.
    atlas::ui::element_handle viewport_;                     ///< Single bounded scroll owner.
    /// @brief Arithmetic source: no object allocated per logical row.
    std::shared_ptr<const atlas::ui::item_source> data_ =
        std::make_shared<atlas::ui::fixed_item_source>(atlas::ui::fixed_item_options{10000, 20});
    std::unique_ptr<atlas::ui::virtual_list> list_; ///< Released before input/tree/dispatcher.
    std::uint64_t frame_ = 0;                       ///< Strictly increasing lifecycle identity.
    std::uint64_t focus_owner_ = 0;                 ///< Native focus ownership across publications.
};
} // namespace

/// @brief Keep interactive reader acceptance outside unattended CTest execution.
int main()
{
    try
    {
        atlas::diagnostics::services sinks;
        sinks.log = std::make_shared<atlas::diagnostics::logger>();
        return virtual_walkthrough{}.run(sinks);
    }
    catch (const std::exception& error)
    {
        std::fputs(error.what(), stderr);
        std::fputc('\n', stderr);
        return 1;
    }
    catch (...)
    {
        return 1;
    }
}
