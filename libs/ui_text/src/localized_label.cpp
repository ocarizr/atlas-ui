/** @file @brief Cleanup-safe localization bridge into native glyph and retained metric updates. */
#include <atlas/ui_text/localized_label.hpp>
#include <stdexcept>
#include <thread>

namespace atlas::ui_text
{
/// @brief Context and label callbacks run on the same owner; retained outputs remain immutable.
struct localized_label::impl
{
    localization::message_key key; ///< Pre-resolved key in the subscribed context.
    text_request options;          ///< Text, locale and direction are replaced during refresh.
    std::string family;            ///< Own the font family instead of borrowing caller storage.
    std::optional<double> count;   ///< Last successfully shaped count.
    diagnostics::services sinks;   ///< Shared optional diagnostic destinations.
    std::thread::id owner = std::this_thread::get_id(); ///< UI mutation affinity.
    std::shared_ptr<const localized_layout> current;    ///< Last complete publication.

    /// @brief Reject API use from workers rather than racing current output.
    void check() const
    {
        if (owner != std::this_thread::get_id())
        {
            throw std::logic_error("localized label belongs to another thread");
        }
    }

    /// @brief All native work and allocation precedes the shape/count publication.
    void update(localization::context& source, std::optional<double> next)
    {
        check();
        const diagnostics::operation_scope scope{sinks, {"ui.text.localize", "ui.text.cpu"}};
        auto candidate = std::make_shared<localized_layout>();
        candidate->content = source.resolve(key, next);
        auto request = options;
        request.text = candidate->content.text;
        request.locale = candidate->content.locale;
        request.family = family;
        request.right_to_left =
            candidate->content.direction == localization::text_direction::right_to_left;
        candidate->layout = shape(request, sinks);
        current = std::move(candidate);
        count = next;
    }
};

/// @brief Observe committed context generations after the initial candidate has shaped
/// successfully.
localized_label::localized_label(localization::context& source, localization::message_key key,
                                 text_request font, std::optional<double> count,
                                 diagnostics::services sinks)
    : state_(std::make_shared<impl>())
{
    state_->key = key;
    state_->options = font;
    state_->family = font.family;
    state_->sinks = std::move(sinks);
    state_->options.text = {};
    state_->options.locale = {};
    state_->options.family = {};
    state_->update(source, count);
    changes_ = source.observe(
        [weak = std::weak_ptr<impl>{state_}, &source](auto)
        {
            if (auto state = weak.lock())
            {
                state->update(source, state->count);
            }
        });
}

/// @brief Token destruction cancels later notifications before private state is released.
localized_label::~localized_label() = default;

/// @brief Read the previous successful revision without borrowing the translation context.
std::shared_ptr<const localized_layout> localized_label::snapshot() const
{
    state_->check();
    return state_->current;
}

/// @brief Count changes follow the same failure-safe refresh path as language notifications.
void localized_label::set_count(localization::context& source, std::optional<double> count)
{
    state_->update(source, count);
}

/// @brief Explicit retry is useful if native shaping failed during a committed context
/// notification.
void localized_label::refresh(localization::context& source)
{
    state_->update(source, state_->count);
}

/// @brief Preserve authored constraints, margins and ordering while updating natural dimensions.
void localized_label::apply_metrics(ui::element_tree& tree, ui::element_handle target) const
{
    state_->check();
    auto style = tree.layout(target);
    style.intrinsic = state_->current->layout.size;
    tree.set_layout(target, style);
}
} // namespace atlas::ui_text
