#include "logging_state.hpp"

#include <algorithm>

#include <stdexcept>

namespace atlas::diagnostics
{
void validate(const logging_options& options)
{
    if (options.level > log_level::off || !options.queue_capacity || !options.max_categories ||
        !options.max_message_bytes || options.category_levels.size() > options.max_categories ||
        (options.rotation_bytes && !options.retained_files))
    {
        throw std::invalid_argument{"Invalid logging level or capacity"};
    }
    for (std::size_t index = 0; index < options.category_levels.size(); ++index)
    {
        const auto& [name, level] = options.category_levels[index];
        if (name.empty() || name.size() > 256 || level > log_level::off)
        {
            throw std::invalid_argument{"Invalid logging category"};
        }
        for (std::size_t previous = 0; previous < index; ++previous)
        {
            if (options.category_levels[previous].first == name)
            {
                throw std::invalid_argument{"Duplicate logging category"};
            }
        }
    }
}

logger::impl::impl(logging_options configuration, std::shared_ptr<log_output> output)
    : options{std::move(configuration)}
{
    validate(options);
    sink = std::make_shared<detail::output_sink>(std::move(output), counts, options);
    if (options.asynchronous)
    {
        pool = std::make_shared<spdlog::details::thread_pool>(options.queue_capacity, 1);
    }
    (void)category("diagnostics");
}

std::shared_ptr<spdlog::logger> logger::impl::category(std::string_view name)
{
    if (const auto found = categories.find(name); found != categories.end())
    {
        return found->second;
    }
    if (categories.size() > options.max_categories)
    {
        throw std::length_error{"Logging category capacity exhausted"};
    }
    std::shared_ptr<spdlog::logger> result;
    if (pool)
    {
        result = std::make_shared<spdlog::async_logger>(
            std::string{name}, sink, pool, spdlog::async_overflow_policy::overrun_oldest);
    }
    else
    {
        result = std::make_shared<spdlog::logger>(std::string{name}, sink);
    }
    result->set_level(spdlog::level::trace);
    result->set_error_handler([state = counts](const std::string&) { ++state->errors; });
    categories.emplace(name, result);
    return result;
}

logger::logger(logging_options options, std::shared_ptr<log_output> output)
    : impl_{std::make_unique<impl>(std::move(options), std::move(output))}
{
}
logger::~logger()
{
    shutdown();
}

bool logger::enabled(log_level level, std::string_view category) const noexcept
{
    if (level >= log_level::off || category.empty() || category.size() > 256)
    {
        return false;
    }
    const auto found =
        std::find_if(impl_->options.category_levels.begin(), impl_->options.category_levels.end(),
                     [&](const auto& entry) { return entry.first == category; });
    const auto minimum =
        found == impl_->options.category_levels.end() ? impl_->options.level : found->second;
    return level >= minimum && minimum != log_level::off;
}

bool logger::write(log_level level, std::string_view category, std::string_view message) noexcept
{
    if (detail::inside_output)
    {
        ++impl_->counts->errors;
        return false;
    }
    try
    {
        const std::lock_guard lock{impl_->admission};
        if (!impl_->accepting)
        {
            return false;
        }
        if (impl_->flushing)
        {
            ++impl_->paused_dropped;
            return false;
        }
        if (level > log_level::off || category.empty() || category.size() > 256 ||
            message.size() > impl_->options.max_message_bytes)
        {
            ++impl_->counts->errors;
            return false;
        }
        const auto found = std::find_if(impl_->options.category_levels.begin(),
                                        impl_->options.category_levels.end(),
                                        [&](const auto& entry) { return entry.first == category; });
        const auto minimum =
            found == impl_->options.category_levels.end() ? impl_->options.level : found->second;
        if (level == log_level::off || level < minimum || minimum == log_level::off)
        {
            ++impl_->counts->filtered;
            return false;
        }
        impl_->category(category)->log(static_cast<spdlog::level::level_enum>(level),
                                       spdlog::string_view_t{message.data(), message.size()});
        ++impl_->counts->submitted;
        return true;
    }
    catch (...)
    {
        ++impl_->counts->errors;
        return false;
    }
}

bool logger::impl::flush_backend() noexcept
{
    const auto errors = counts->errors.load();
    try
    {
        if (pool)
        {
            std::unique_lock lock{counts->mutex};
            const auto target = counts->flush_count + 1;
            pool->post_flush(
                std::static_pointer_cast<spdlog::async_logger>(categories.at("diagnostics")),
                spdlog::async_overflow_policy::block);
            counts->flushed.wait(lock, [&] { return counts->flush_count >= target; });
        }
        else
        {
            sink->flush();
        }
    }
    catch (...)
    {
        ++counts->errors;
    }
    return counts->errors.load() == errors;
}

bool logger::flush() noexcept
{
    if (detail::inside_output)
    {
        ++impl_->counts->errors;
        return false;
    }
    const std::lock_guard lifecycle{impl_->lifecycle};
    {
        const std::lock_guard lock{impl_->admission};
        if (!impl_->accepting)
        {
            return true;
        }
        impl_->flushing = true;
    }
    const auto result = impl_->flush_backend();
    {
        const std::lock_guard lock{impl_->admission};
        impl_->flushing = false;
    }
    return result;
}

void logger::shutdown() noexcept
{
    if (detail::inside_output)
    {
        ++impl_->counts->errors;
        return;
    }
    const std::lock_guard lifecycle{impl_->lifecycle};
    {
        const std::lock_guard lock{impl_->admission};
        if (!impl_->accepting)
        {
            return;
        }
        impl_->accepting = false;
        impl_->flushing = true;
    }
    (void)impl_->flush_backend();
    const std::lock_guard lock{impl_->admission};
    if (impl_->pool)
    {
        impl_->retired_dropped = impl_->pool->overrun_counter();
    }
    impl_->categories.clear();
    impl_->pool.reset();
}

log_statistics logger::statistics() const
{
    if (detail::inside_output)
    {
        throw std::logic_error{"Output cannot reenter its logger"};
    }
    const std::lock_guard lock{impl_->admission};
    return {impl_->counts->submitted.load(), impl_->counts->filtered.load(),
            impl_->counts->written.load(),
            impl_->paused_dropped +
                (impl_->pool ? impl_->pool->overrun_counter() : impl_->retired_dropped),
            impl_->counts->errors.load()};
}
} // namespace atlas::diagnostics
