/** @file @brief Instance construction, typed direct reads and explicit frame/recovery lifecycle. */
#include "runtime_state.hpp"
#include <algorithm>

namespace atlas::mvvm
{
/// @brief Reserve before dispatch; shared PMR owns numeric columns and transport metadata.
view_model::impl::impl(const schema& source, runtime_limits policy,
                       std::shared_ptr<std::pmr::memory_resource> resource,
                       diagnostics::services services)
    : description(source), layout(description.data_), limits(policy), memory(std::move(resource)),
      destinations(std::move(services)), booleans(0, memory.get()), integers(0, memory.get()),
      numbers(0, memory.get()), texts(0, memory.get()), queue(0, memory.get()),
      staged(0, memory.get()), arguments(0, memory.get()), latest(0, memory.get()),
      buckets(0, memory.get()), listeners(0, memory.get())
{
    booleans.resize(layout->counts[0]);
    integers.resize(layout->counts[1]);
    numbers.resize(layout->counts[2]);
    texts.reserve(layout->counts[3]);
    for (std::size_t i = 0; i < layout->counts[3]; ++i)
    {
        texts.emplace_back();
    }

    staged.reserve(std::min(layout->max_arguments, limits.max_arguments));
    queue.reserve(limits.max_commands);
    arguments.reserve(limits.max_arguments);
    buckets.resize(layout->source.fields.size() + layout->source.actions.size() +
                   layout->source.events.size());
    listeners.reserve(limits.max_subscriptions);
    latest.resize(layout->source.fields.size());
    for (std::size_t i = 0; i < layout->source.fields.size(); ++i)
    {
        commit(static_cast<std::uint32_t>(i), layout->source.fields[i].initial);
    }
}

/// @brief Only immutable thread identity is inspected before rejecting a foreign caller.
void view_model::impl::owner() const
{
    if (thread != std::this_thread::get_id())
    {
        throw std::logic_error("MVVM requires its owner thread");
    }
}

/// @brief Demands are permitted inside callbacks but never before frame admission or after failure.
void view_model::impl::writable() const
{
    owner();
    if (admitting || !stats.frame || stats.failed)
    {
        throw std::logic_error("MVVM needs a healthy admitted frame");
    }
}

/// @brief A type-specific column access is one identity/range/type check plus an indexed load.
std::uint32_t view_model::impl::offset(field_handle slot, value_type type) const
{
    owner();
    if (description.type(slot) != type)
    {
        throw std::invalid_argument("MVVM field type mismatch");
    }

    return layout->offsets[slot.index];
}

/// @brief Runtime limits never silently weaken schema type or string validation.
view_model::view_model(const schema& description, runtime_limits limits,
                       std::shared_ptr<std::pmr::memory_resource> memory,
                       const diagnostics::services& destinations)
{
    const diagnostics::operation_scope scope{destinations, {"mvvm.create", "mvvm.cpu"}};
    if (!limits.max_commands || limits.max_commands > 65536 || !limits.max_arguments ||
        limits.max_arguments > 1048576 || !limits.max_payload_bytes ||
        limits.max_payload_bytes > 16777216 || !limits.max_subscriptions ||
        limits.max_subscriptions > 65536 || !limits.max_callbacks ||
        limits.max_callbacks > 1048576 || limits.max_warning_details > 65536)
    {
        throw std::invalid_argument("invalid MVVM runtime limits");
    }

    if (!memory)
    {
        memory = {std::pmr::new_delete_resource(), [](std::pmr::memory_resource*)
                  {
                  }};
    }

    impl_ = std::make_unique<impl>(description, limits, std::move(memory), destinations);
    impl_->facade = this;
}

/// @brief Destroying mutable state from a callback or foreign thread is a contract violation.
view_model::~view_model()
{
    if (impl_->thread != std::this_thread::get_id() || impl_->flushing || impl_->admitting)
    {
        std::terminate();
    }

    impl_->flushing = true;
    impl_->admitting = true;
}

/// @brief Metadata observations are safe on the owner during notifications.
const schema& view_model::description() const
{
    impl_->owner();
    return impl_->description;
}

/// @brief Logical values use byte columns instead of shared bit proxies.
bool view_model::boolean(field_handle slot) const
{
    return impl_->booleans[impl_->offset(slot, value_type::boolean)] != 0;
}

/// @brief No name lookup or variant visitation occurs in this typed read.
std::int64_t view_model::integer(field_handle slot) const
{
    return impl_->integers[impl_->offset(slot, value_type::integer)];
}

/// @brief Numbers retain their authored double precision.
double view_model::number(field_handle slot) const
{
    return impl_->numbers[impl_->offset(slot, value_type::number)];
}

/// @brief Text views are borrowed across reads, not across commits to that field.
std::string_view view_model::text(field_handle slot) const
{
    return impl_->texts[impl_->offset(slot, value_type::text)];
}

/// @brief Generic values intentionally own their text boundary payload.
value view_model::impl::read(field_handle slot) const
{
    owner();
    const auto type = description.type(slot);
    const auto index = layout->offsets[slot.index];
    switch (type)
    {
        case value_type::boolean:
            return booleans[index] != 0;
        case value_type::integer:
            return integers[index];
        case value_type::number:
            return numbers[index];
        case value_type::text:
            return std::string{texts[index]};
    }

    throw std::logic_error("invalid compiled MVVM field type");
}

/// @brief Own the result rather than leaking mutable instance storage.
value view_model::read(field_handle slot) const
{
    return impl_->read(slot);
}

/// @brief Failed or unfinished work must be explicitly resolved before a new frame.
void view_model::begin_frame(std::uint64_t frame)
{
    impl_->owner();
    if (impl_->flushing || impl_->admitting || impl_->stats.failed || !impl_->queue.empty())
    {
        throw std::logic_error("drain or reset MVVM before beginning a frame");
    }

    if (!frame || frame <= impl_->stats.frame)
    {
        throw std::invalid_argument("MVVM frame must increase");
    }

    impl_->stats = {frame, 0, 0, 0, false};
    impl_->payload_bytes = impl_->admitted_arguments = 0;
    impl_->segment = 1;
    std::fill(impl_->latest.begin(), impl_->latest.end(), pending_field{});
}

/// @brief Recovery preserves committed values and registrations; client effects are not rolled
/// back.
void view_model::reset()
{
    impl_->owner();
    if (impl_->flushing || impl_->admitting)
    {
        throw std::logic_error("MVVM reset during dispatch");
    }

    impl_->queue.clear();
    impl_->arguments.clear();
    impl_->stats.failed = false;
    ++impl_->segment;
}

/// @brief Statistics are observed synchronously on the owner.
statistics view_model::inspect() const
{
    impl_->owner();
    return impl_->stats;
}
} // namespace atlas::mvvm
