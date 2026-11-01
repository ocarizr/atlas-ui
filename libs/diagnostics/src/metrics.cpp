/**
 * @file
 * @brief Bounded numeric counter/gauge aggregation and explicit transport-neutral snapshots.
 */

#include "metrics_state.hpp"
#include "profiling_state.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace atlas::diagnostics
{
namespace
{
/// @brief Validate bounded UTF-8 metric identifiers/attributes and reject ASCII controls.
bool valid_text(std::string_view text, std::size_t limit, bool allow_empty)
{
    return (allow_empty || !text.empty()) && text.size() <= limit && detail::valid_utf8(text) &&
           std::none_of(text.begin(), text.end(),
                        [](unsigned char byte)
                        {
                            return byte < 0x20 || byte == 0x7f;
                        });
}
} // namespace

/// @brief Reject invalid configuration, handles or descriptors before mutating execution state.
void validate(const metrics_options& options)
{
    if (!options.max_series || !options.max_text_bytes)
    {
        throw std::invalid_argument{"Metrics series and text limits must be positive"};
    }
}

/// @brief Create an empty bounded numeric registry; observations and reporting are explicit caller
/// operations.
metrics::metrics(metrics_options options) : impl_{std::make_unique<impl>(options)}
{
}

/// @brief Release metrics resources and complete its scoped cleanup.
metrics::~metrics() = default;

/// @brief Validate and canonicalize one numeric observation before applying a bounded registry
/// update.
bool metrics::impl::record(std::string_view name, double number,
                           const metric_attributes& attributes, metric_kind kind) noexcept
{
    if (!options.enabled)
    {
        return false;
    }

    try
    {
        if (!std::isfinite(number) || (kind == metric_kind::counter && number < 0) ||
            !valid_text(name, options.max_text_bytes, false) ||
            attributes.size() > options.max_attributes)
        {
            ++rejected;
            return false;
        }

        for (const auto& [attribute, text] : attributes)
        {
            if (!valid_text(attribute, options.max_text_bytes, false) ||
                !valid_text(text, options.max_text_bytes, true))
            {
                ++rejected;
                return false;
            }
        }

        key identity{std::string{name}, attributes};
        std::sort(identity.second.begin(), identity.second.end());
        if (std::adjacent_find(identity.second.begin(), identity.second.end(),
                               [](const auto& left, const auto& right)
                               {
                                   return left.first == right.first;
                               }) != identity.second.end())
        {
            ++rejected;
            return false;
        }

        return update(std::move(identity), number, kind);
    }
    catch (...)
    {
        ++rejected;
        return false;
    }
}

/// @brief Apply one canonical metric update under the registry lock, enforcing type and series
/// limits.
bool metrics::impl::update(key identity, double number, metric_kind kind)
{
    const std::lock_guard lock{mutex};
    const auto first = values.lower_bound(key{identity.first, {}});
    if (first != values.end() && first->first.first == identity.first && first->second.kind != kind)
    {
        ++rejected;
        return false;
    }

    const auto found = values.find(identity);
    if (found == values.end())
    {
        if (values.size() >= options.max_series)
        {
            ++dropped;
            return false;
        }

        values.emplace(std::move(identity), value{kind, number});
    }
    else
    {
        const auto next = kind == metric_kind::counter ? found->second.number + number : number;
        if (!std::isfinite(next))
        {
            ++rejected;
            return false;
        }

        found->second.number = next;
    }

    ++accepted;
    return true;
}

/// @brief Add a finite nonnegative amount to a cumulative counter; invalid/overflowing updates
/// preserve its value.
bool metrics::increment(std::string_view name, double amount,
                        const metric_attributes& attributes) noexcept
{
    return impl_->record(name, amount, attributes, metric_kind::counter);
}

/// @brief Replace a gauge with a finite value while preserving stable instrument kind per name.
bool metrics::set(std::string_view name, double value, const metric_attributes& attributes) noexcept
{
    return impl_->record(name, value, attributes, metric_kind::gauge);
}

/// @brief Copy a consistent owning cumulative registry view without resetting counters or gauges.
metric_snapshot metrics::snapshot() const
{
    const std::lock_guard lock{impl_->mutex};
    metric_snapshot result;
    result.started = impl_->started;
    result.captured = std::chrono::system_clock::now();
    result.points.reserve(impl_->values.size());
    for (const auto& [identity, value] : impl_->values)
    {
        result.points.push_back({identity.first, identity.second, value.kind, value.number});
    }

    return result;
}

/// @brief Return an observational snapshot of this instance's counters without resetting them.
metrics_statistics metrics::statistics() const
{
    const std::lock_guard lock{impl_->mutex};
    return {impl_->values.size(),  impl_->accepted.load(), impl_->rejected.load(),
            impl_->dropped.load(), impl_->exported.load(), impl_->export_errors.load()};
}

/// @brief Synchronously publish one cumulative owning snapshot outside registry locks; failures
/// never consume values.
bool metrics::report(metrics_output& output) noexcept
{
    if (!impl_->options.enabled)
    {
        return false;
    }

    try
    {
        const auto batch = snapshot();
        output.publish(batch);
        ++impl_->exported;
        return true;
    }
    catch (...)
    {
        ++impl_->export_errors;
        return false;
    }
}
} // namespace atlas::diagnostics
