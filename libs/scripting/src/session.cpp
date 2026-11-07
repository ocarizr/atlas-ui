/** @file @brief Owner lifecycle, transactional VM reload and explicit model frame boundaries. */
#include "session_state.hpp"
#include <algorithm>
#include <stdexcept>

namespace atlas::scripting
{
/// @brief Reject foreign owner, closed state and reentrant operations before mutation.
void session::impl::check() const
{
    if (owner != std::this_thread::get_id())
    {
        throw std::logic_error("scripting requires its owner thread");
    }

    if (busy || delivering || closed)
    {
        throw std::logic_error("scripting is closed or reentrant");
    }
}

/// @brief Release all model subscriptions before replacing their interpreter generation.
void session::impl::detach()
{
    for (const auto& watch : watches)
    {
        model->unsubscribe(watch.token);
    }

    watches.clear();
}

/// @brief Latch script failure and cancel outstanding native work without undoing committed state.
void session::impl::fail()
{
    stats.failed = true;
    mail->close();
    diagnostics::write(destinations, diagnostics::log_level::warning, "scripting",
                       "behavior failed; reload required; prior committed model state retained");
}

/// @brief Validate limits and retain the composition-root model, allocator and diagnostics.
session::session(std::shared_ptr<mvvm::view_model> model, session_limits limits,
                 std::shared_ptr<std::pmr::memory_resource> memory,
                 diagnostics::services destinations)
    : impl_(std::make_shared<impl>())
{
    if (!model || limits.max_memory_bytes < 65536 ||
        limits.max_memory_bytes > std::size_t{64} * 1024 * 1024 || !limits.max_source_bytes ||
        limits.max_source_bytes > 1048576 || !limits.max_commands || limits.max_commands > 4096 ||
        !limits.max_text_bytes || limits.max_text_bytes > 1048576 || !limits.max_pending_work ||
        limits.max_pending_work > 1024 || !limits.max_calls || limits.max_calls > 65536 ||
        limits.max_instructions < 100 || limits.max_instructions > 10000000 ||
        !limits.max_pending_bytes || limits.max_pending_bytes > std::size_t{64} * 1024 * 1024)
    {
        throw std::invalid_argument("invalid scripting session limits/model");
    }

    (void)model->description();
    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](std::pmr::memory_resource*)
                  {
                  }};
    }

    impl_->model = std::move(model);
    impl_->memory = std::move(memory);
    impl_->limits = limits;
    impl_->destinations = std::move(destinations);
    impl_->watches.reserve(1024);
}

/// @brief Require owner-thread quiescence before cancelling work and releasing interpreter state.
session::~session()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy || impl_->delivering)
    {
        std::terminate();
    }

    try
    {
        shutdown();
    }
    catch (...)
    {
        std::terminate();
    }
}

/// @brief Prepare a candidate interpreter before retiring the current healthy generation.
void session::load(std::string_view source, std::string_view source_name)
{
    impl_->check();
    if (source.empty() || source.size() > impl_->limits.max_source_bytes)
    {
        throw std::invalid_argument("invalid script source size");
    }

    mvvm::validate_text(source_name, 1024);
    if (source_name.empty() || source_name.find_first_of("\r\n") != std::string_view::npos)
    {
        throw std::invalid_argument("invalid script source identity");
    }

    const detail::busy_guard guard{impl_->busy};
    const diagnostics::operation_scope scope{impl_->destinations, {"script.load", "script.cpu"}};
    auto candidate = std::make_unique<detail::lua_vm>(impl_->model, impl_->limits, impl_->memory);
    candidate->load(source, source_name);
    auto mail = std::make_shared<detail::mailbox>();
    mail->pending.reserve(impl_->limits.max_pending_work);
    mail->max_text = impl_->limits.max_text_bytes;
    mail->limit = impl_->limits.max_pending_bytes;
    impl_->detach();
    if (impl_->mail)
    {
        impl_->mail->close();
    }

    impl_->vm = std::move(candidate);
    impl_->mail = std::move(mail);
    impl_->stats = {};
    diagnostics::write(impl_->destinations, diagnostics::log_level::debug, "scripting",
                       "loaded behavior generation; prior observers and native work retired");
}

/// @brief Admit increasing frames only when the model has the same healthy identity.
void session::begin_frame(std::uint64_t frame)
{
    impl_->check();
    const auto state = impl_->model->inspect();
    if (!impl_->vm || impl_->stats.failed || !frame || frame <= impl_->stats.frame ||
        state.frame != frame || state.failed)
    {
        throw std::logic_error("scripting needs a loaded healthy matching model frame");
    }

    impl_->stats.frame = frame;
    impl_->stats.calls = 0;
}

/// @brief Enter the guarded owner-thread producer boundary; never flush the model implicitly.
std::vector<mvvm::value> session::call(std::string_view handler, std::span<const mvvm::value> args,
                                       std::span<const mvvm::value_type> result_types)
{
    return impl_->call(handler, args, false, result_types);
}

/// @brief Cancel transport and release observers without waiting for caller-owned jobs.
void session::shutdown()
{
    if (impl_->owner != std::this_thread::get_id() || impl_->busy || impl_->delivering)
    {
        throw std::logic_error("invalid scripting shutdown owner/reentry");
    }

    if (impl_->closed)
    {
        return;
    }

    const detail::busy_guard guard{impl_->busy};
    impl_->closed = true;
    impl_->detach();
    if (impl_->mail)
    {
        impl_->mail->close();
    }

    impl_->vm.reset();
    diagnostics::write(impl_->destinations, diagnostics::log_level::debug, "scripting",
                       "closed behavior session and cancelled outstanding native work");
}

/// @brief Observe owner statistics and locked transport counts without entering Lua.
statistics session::inspect() const
{
    impl_->check();
    auto result = impl_->stats;
    if (impl_->vm)
    {
        result.lua_bytes = impl_->vm->memory.used;
        result.lua_peak_bytes = impl_->vm->memory.peak;
    }

    if (impl_->mail)
    {
        const std::lock_guard lock{impl_->mail->mutex};
        result.pending_work = impl_->mail->pending.size();
    }

    return result;
}
} // namespace atlas::scripting
