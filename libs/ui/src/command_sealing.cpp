/** @file @brief Deterministic command preparation implementation. */
#include "command_state.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <tuple>

namespace atlas::ui
{
namespace
{
/// @brief Preserve serial order inside each independent property key.
auto sort_key(const property_write& write)
{
    return std::tuple{write.target.session, write.target.index, write.target.generation,
                      write.field, write.sequence};
}

/// @brief Only absolute writes to the exact same target generation/property may coalesce.
bool same_key(const property_write& first, const property_write& second)
{
    return first.target == second.target && first.field == second.field;
}

/// @brief Reset even after allocation failure or contained diagnostic reentrancy.
class sealing_guard
{
  public:
    explicit sealing_guard(bool& flag) : flag_(flag)
    {
        flag_ = true;
    } ///< Enter after owner checks.

    ~sealing_guard()
    {
        flag_ = false;
    } ///< Restore admission on every exit.

    sealing_guard(const sealing_guard&) = delete; ///< One lexical operation owns the flag.
    sealing_guard& operator=(const sealing_guard&) = delete; ///< Never transfer guard ownership.
  private:
    bool& flag_; ///< Owner-only producer state.
};

/// @brief Logging is bounded and observational; immutable conflict records remain authoritative.
void report(const detail::command_data& data, const diagnostics::services& destinations,
            std::size_t detail_limit) noexcept
{
    if (data.conflicts.empty() || !destinations.log ||
        !destinations.log->enabled(diagnostics::log_level::warning, "ui.commands"))
    {
        return;
    }

    std::array<char, 384> text{};
    (void)std::snprintf(text.data(), text.size(),
                        "UI command conflicts: frame=%llu batch=%u keys=%zu; last write selected",
                        static_cast<unsigned long long>(data.frame), data.ordinal,
                        data.conflicts.size());
    diagnostics::write(destinations, diagnostics::log_level::warning, "ui.commands", text.data());
    constexpr std::array names{"width", "height", "opacity", "visible"};
    const auto count = std::min(detail_limit, data.conflicts.size());
    for (std::size_t index = 0; index < count; ++index)
    {
        const auto& conflict = data.conflicts[index];
        const auto& last = conflict.replacement;
        (void)std::snprintf(
            text.data(), text.size(),
            "frame=%llu batch=%u element=%u:%u property=%s producer=%llu sequence=%llu -> "
            "producer=%llu sequence=%llu writes=%zu",
            static_cast<unsigned long long>(data.frame), data.ordinal, last.target.index,
            last.target.generation, names[static_cast<std::size_t>(last.field)],
            static_cast<unsigned long long>(conflict.previous.producer),
            static_cast<unsigned long long>(conflict.previous.sequence),
            static_cast<unsigned long long>(last.producer),
            static_cast<unsigned long long>(last.sequence), conflict.writes_this_frame);
        diagnostics::write(destinations, diagnostics::log_level::warning, "ui.commands",
                           text.data());
    }
}
} // namespace

/// @brief Sort a private snapshot, then commit once all potentially failing preparation succeeds.
command_batch command_buffer::seal()
{
    impl_->check();
    if (impl_->ordinal >= impl_->limits.max_batches)
    {
        throw std::overflow_error("UI frame batch capacity exhausted");
    }

    const sealing_guard guard{impl_->sealing};
    const diagnostics::operation_scope scope{impl_->destinations, {"ui.commands.seal", "ui.cpu"}};
    auto data =
        std::make_shared<detail::command_data>(impl_->frame, impl_->memory, impl_->ordinal + 1);
    data->session = impl_->session;
    std::pmr::vector<property_write> sorted{impl_->history, impl_->memory.get()};
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& first, const auto& second)
              {
                  return sort_key(first) < sort_key(second);
              });
    const auto pending = impl_->history.size() - impl_->sealed;
    data->writes.reserve(pending);
    data->conflicts.reserve(pending);
    for (std::size_t start = 0; start < sorted.size();)
    {
        auto end = start + 1;
        while (end < sorted.size() && same_key(sorted[start], sorted[end]))
        {
            ++end;
        }

        const auto& last = sorted[end - 1];
        if (last.sequence > impl_->sealed)
        {
            data->writes.push_back(last);
            if (end - start > 1)
            {
                data->conflicts.push_back({sorted[end - 2], last, end - start});
            }
        }

        start = end;
    }

    std::sort(data->writes.begin(), data->writes.end(),
              [](const auto& first, const auto& second)
              {
                  return first.sequence < second.sequence;
              });
    std::sort(data->conflicts.begin(), data->conflicts.end(),
              [](const auto& first, const auto& second)
              {
                  return first.replacement.sequence < second.replacement.sequence;
              });
    impl_->sealed = impl_->history.size();
    impl_->ordinal = data->ordinal;
    report(*data, impl_->destinations, impl_->limits.max_warning_details);
    return command_batch{std::move(data)};
}
} // namespace atlas::ui
