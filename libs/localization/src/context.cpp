/** @file @brief Checked per-context key resolution and cardinal translation selection. */
#include "state.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace atlas::localization
{
namespace
{
/// @brief Allocate key ownership identities without permitting rollover/reuse.
std::uint64_t next_identity()
{
    static std::atomic<std::uint64_t> next{1};
    auto value = next.load();
    do
    {
        if (value == std::numeric_limits<std::uint64_t>::max())
        {
            throw std::overflow_error("localization identities exhausted");
        }
    } while (!next.compare_exchange_weak(value, value + 1));

    return value;
}
} // namespace

/// @brief Construction validates native or XML-built definitions through the same entry point.
context::context(definition source, plural_selector selector, diagnostics::services sinks)
    : state_(std::make_shared<detail::context_state>())
{
    state_->sinks = std::move(sinks);
    const diagnostics::operation_scope scope{state_->sinks,
                                             {"localization.construct", "localization.cpu"}};
    if (!selector)
    {
        throw std::invalid_argument("localization requires a plural selector");
    }

    state_->identity = next_identity();
    state_->selector = std::move(selector);
    auto tables = detail::prepare(std::move(source));
    state_->revision = 0;
    state_->publish(tables, tables->data.fallback);
}

/// @brief No callbacks are emitted during context teardown.
context::~context()
{
    for (const auto& observer : state_->observers)
    {
        if (auto live = observer.lock())
        {
            live->active = false;
        }
    }
}

/// @brief Resolve source names only during binding construction.
message_key context::key(std::string_view name) const
{
    state_->check();
    const auto& keys = state_->tables->keys;
    const auto found = std::lower_bound(keys.begin(), keys.end(), name);
    if (found == keys.end() || *found != name)
    {
        throw std::out_of_range("unknown localization key");
    }

    return {state_->identity, static_cast<std::uint32_t>(found - keys.begin())};
}

/// @brief A compact handle indexes the already-resolved translation and its locale.
resolved_text context::resolve(message_key key, std::optional<double> count) const
{
    state_->check();
    const diagnostics::operation_scope scope{state_->sinks,
                                             {"localization.resolve", "localization.cpu"}};
    if (key.context != state_->identity || key.index >= state_->resolved.size())
    {
        throw std::invalid_argument("foreign or invalid localization key");
    }

    if (count && !std::isfinite(*count))
    {
        throw std::invalid_argument("nonfinite localization count");
    }

    const auto& [table, entry] = state_->resolved[key.index];
    const std::string* text = nullptr;
    if (entry->text)
    {
        text = &*entry->text;
    }
    else
    {
        if (!count)
        {
            throw std::invalid_argument("plural localization message requires count");
        }

        const auto category = state_->selector(table->tag, *count);
        if (category > plural_category::other)
        {
            throw std::invalid_argument("invalid plural category from adapter");
        }

        const auto& form = entry->forms[static_cast<std::size_t>(category)];
        text = form ? &*form : &*entry->forms[5];
    }

    return {*text, table->tag, table->direction, state_->revision};
}

/// @brief Return a copy so later publications cannot invalidate a borrowed tag.
std::string context::selected_language() const
{
    state_->check();
    return state_->selected;
}

/// @brief Read committed revision only on the owner.
std::uint64_t context::revision() const
{
    state_->check();
    return state_->revision;
}

/// @brief Normalize and prepare complete fallback slots before a locale swap.
void context::select(std::string_view language)
{
    auto state = state_;
    state->check(true);
    if (language == state->selected)
    {
        return;
    }

    auto tag = normalize_tag(language);
    if (tag == state->selected)
    {
        return;
    }

    state->publish(state->tables, std::move(tag));
}

/// @brief Reload cannot change the binding schema or publish a cancelled candidate.
void context::reload(definition source, const std::stop_token& stop)
{
    auto state = state_;
    state->check(true);
    const diagnostics::operation_scope scope{state->sinks,
                                             {"localization.reload", "localization.cpu"}};
    if (stop.stop_requested())
    {
        throw std::runtime_error("localization reload cancelled");
    }

    auto candidate = detail::prepare(std::move(source));
    if (candidate->keys != state->tables->keys)
    {
        throw std::invalid_argument("localization reload changed key schema");
    }

    if (stop.stop_requested())
    {
        throw std::runtime_error("localization reload cancelled");
    }

    state->publish(std::move(candidate), state->selected);
}
} // namespace atlas::localization
