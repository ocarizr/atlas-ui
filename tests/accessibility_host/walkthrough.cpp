/**
 * @file
 * @brief Interactive reader fixture for the public window/channel/adapter contract.
 */
#include <algorithm>
#include <atlas/accessibility_host/adapter.hpp>
#include <cstdio>
#include <iostream>

namespace
{
namespace semantics = atlas::accessibility;

/// @brief Deterministic owner state; no application handlers run on native query threads.
class walkthrough
{
  public:
    /// @brief Keep the endpoint attached until the channel closes before window destruction.
    int run(const atlas::diagnostics::services& sinks)
    {
        if (!atlas::accessibility_host::available())
        {
            std::cerr << "A native accessibility backend is required.\n";
            return 2;
        }

        atlas::window::window window{{"Atlas Accessibility Walkthrough", 720, 540}};
        const auto source = std::make_shared<semantics::channel>(semantics::limits{}, sinks);
        atlas::accessibility_host::adapter host{window, source, sinks};
        while (!window.poll_events().close_requested && !closed_)
        {
            for (const auto& request : source->take_requests())
            {
                ++native_requests_;
                std::cout << "Native request " << static_cast<int>(request.operation) << " for "
                          << request.target.index << ':' << request.target.generation << '\n';
                apply(request);
            }

            const auto input = window.take_input();
            for (const auto& event : input.values())
            {
                if (event.kind == atlas::input::event_kind::control_changed)
                {
                    last_control_ = event.control;
                }

                if (event.device.kind == atlas::input::device_kind::keyboard &&
                    event.kind == atlas::input::event_kind::control_changed && event.value.x == 0)
                {
                    key(event.control);
                }
            }

            auto nodes = snapshot(host.coordinates());
            const auto prior = source->read();
            if (!prior || !std::ranges::equal(prior->nodes(), nodes))
            {
                source->publish(std::move(nodes));
                host.notify();
            }

            window.wait_for_events(16);
        }

        source->close();
        host.notify();
        return 0;
    }

  private:
    /// @brief Build one current generation with stable hierarchy and physical screen bounds.
    std::vector<semantics::node>
    snapshot(atlas::accessibility_host::screen_transform coordinates) const
    {
        std::vector<semantics::node> nodes;
        nodes.reserve(18);
        const auto add = [&](std::uint32_t id, std::uint32_t parent, semantics::role role,
                             std::string name) -> semantics::node&
        {
            semantics::node node{{id, id == 11 ? generation_ : 1, 187},
                                 parent ? semantics::identity{parent, 1, 187}
                                        : semantics::identity{},
                                 role,
                                 std::move(name)};
            node.bounds = {coordinates.x + 20,
                           coordinates.y + 15 + static_cast<double>(nodes.size()) * 24., 500, 22};
            node.focusable = role != semantics::role::group && role != semantics::role::tab_group &&
                             role != semantics::role::list;
            node.focused = id == focused_;
            node.invoke = role == semantics::role::button || role == semantics::role::checkbox;
            nodes.push_back(std::move(node));
            return nodes.back();
        };
        if (modal_)
        {
            add(20, 0, semantics::role::group, "Confirmation dialog");
            add(21, 20, semantics::role::button, "Close dialog");
            return nodes;
        }

        add(1, 0, semantics::role::group, "Menu controls");
        add(2, 1, semantics::role::button, french_ ? "Ouvrir les options" : "Open settings")
            .description = french_ ? "Ouvre la confirmation" : "Opens the confirmation dialog";
        add(3, 1, semantics::role::checkbox, "Music").checked = music_;
        auto& volume = add(4, 1, semantics::role::slider, "Volume");
        volume.number = 25;
        volume.maximum = 100;
        add(5, 1, semantics::role::button, "Unavailable action").enabled = false;
        add(6, 0, semantics::role::tab_group, "Settings pages");
        for (const auto id : {7U, 8U})
        {
            auto& tab = add(id, 6, semantics::role::tab, id == 7 ? "General" : "Audio");
            tab.select = true;
            tab.selected = id == selected_;
        }

        add(9, 0, semantics::role::list, "Realized inventory rows");
        add(10, 9, semantics::role::list_item, "Inventory row one");
        if (!removed_)
        {
            auto& row = add(11, 9, semantics::role::list_item,
                            generation_ == 1 ? "Inventory row two" : "Inventory recycled row");
            row.reveal = true;
            row.offscreen = !revealed_;
            if (row.offscreen)
            {
                row.bounds.y += 1000;
            }
        }

        add(12, 0, semantics::role::button, "Change language");
        add(13, 0, semantics::role::button, "Recycle inventory row");
        add(14, 0, semantics::role::button, "Remove inventory row");
        add(15, 0, semantics::role::button, "Clear tab selection");
        add(16, 0, semantics::role::text, "Status").value =
            music_ ? "Music enabled" : "Music disabled";
        add(18, 0, semantics::role::text, "Native request count").value =
            std::to_string(native_requests_) + "; last input " + std::to_string(last_control_);
        add(17, 0, semantics::role::button, "Close walkthrough");
        return nodes;
    }

    /// @brief Recheck active scope before applying queued semantic intentions.
    void apply(const semantics::request& request)
    {
        const auto id = request.target.index;
        if ((modal_ && id != 21) || (!modal_ && id == 21) || id == 5 ||
            (id == 11 && (removed_ || request.target.generation != generation_)))
        {
            return;
        }

        if (request.operation == semantics::action::focus ||
            request.operation == semantics::action::reveal)
        {
            focused_ = id;
            revealed_ = revealed_ || id == 11;
            return;
        }

        focused_ = id;
        switch (id)
        {
            case 2:
                modal_ = true;
                focused_ = 21;
                break;
            case 3:
                music_ = !music_;
                break;
            case 7:
            case 8:
                selected_ = id;
                break;
            case 12:
                french_ = !french_;
                break;
            case 13:
                ++generation_;
                removed_ = false;
                focused_ = 11;
                revealed_ = true;
                break;
            case 14:
                removed_ = true;
                break;
            case 15:
                selected_ = 0;
                break;
            case 17:
                closed_ = true;
                break;
            case 21:
                modal_ = false;
                focused_ = 2;
                break;
            default:
                break;
        }
    }

    /// @brief Shortcuts only focus targets; activation must arrive through the native provider.
    void key(std::uint32_t control)
    {
        if (control >= '1' && control <= '9')
        {
            constexpr std::uint32_t targets[]{2, 3, 4, 8, 11, 12, 13, 14, 15};
            apply({{targets[control - '1'], targets[control - '1'] == 11 ? generation_ : 1, 187},
                   semantics::action::focus});
        }
        else if (control == static_cast<std::uint32_t>(atlas::input::key::escape))
        {
            if (modal_)
            {
                modal_ = false;
                focused_ = 2;
            }
            else
            {
                closed_ = true;
            }
        }
    }

    std::uint32_t focused_ = 2;         ///< Single owner keyboard identity.
    std::uint32_t native_requests_ = 0; ///< Native provider requests, independent of key shortcuts.
    std::uint32_t last_control_ = 0;    ///< Portable control most recently delivered by the window.
    std::uint32_t selected_ = 0;        ///< Empty selection is a supported transient state.
    std::uint32_t generation_ = 1;      ///< Recycled logical slot cannot alias an old provider.
    bool modal_ = false;                ///< Covered controls are absent from the published domain.
    bool music_ = true;                 ///< Toggle state independent of focus.
    bool french_ = false;   ///< Simulated owner language resolution, never native-thread lookup.
    bool revealed_ = false; ///< Realized row reveal; not generic virtual-item realization.
    bool removed_ = false;  ///< Removed slots disappear from reader traversal.
    bool closed_ = false;   ///< Deterministic ordered disconnection.
};
} // namespace

/// @brief Run manually with Narrator; never register this interactive loop as an unattended test.
int main()
{
    try
    {
        atlas::diagnostics::services sinks;
        sinks.log = std::make_shared<atlas::diagnostics::logger>();
        return walkthrough{}.run(sinks);
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
