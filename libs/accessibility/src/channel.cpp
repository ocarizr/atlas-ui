/** @file @brief Immutable publication and synchronized bounded request admission. */
#include <atlas/accessibility/channel.hpp>
#include <mutex>
#include <thread>

namespace atlas::accessibility
{
/// @brief Native readers share values only, never UI owner callbacks.
class channel::impl
{
  public:
    limits bounds;                                      ///< Validated budgets.
    diagnostics::services sinks;                        ///< Optional owner instrumentation.
    std::thread::id owner = std::this_thread::get_id(); ///< Publication affinity.
    mutable std::mutex mutex; ///< Protect snapshot, queue and disconnection together.
    std::shared_ptr<const snapshot> current;      ///< Immutable published view.
    std::vector<accessibility::request> requests; ///< Bounded action values.
    bool closed = false;                          ///< Terminal disconnect.

    void check() const
    {
        if (owner != std::this_thread::get_id())
        {
            throw std::logic_error("accessibility publication requires its owner thread");
        }
    } ///< Never call application code from a native client thread.
};

/// @brief Adopt already validated nodes without materializing another tree.
snapshot::snapshot(std::vector<node> nodes) : nodes_(std::move(nodes))
{
}

/// @brief Snapshot values remain stable while retained.
std::span<const node> snapshot::nodes() const noexcept
{
    return nodes_;
}

/// @brief Generation-safe linear lookup avoids sparse high-slot allocations.
const node* snapshot::find(identity id) const noexcept
{
    for (const auto& value : nodes_)
    {
        if (value.id == id)
        {
            return &value;
        }
    }

    return nullptr;
}

/// @brief Bound queue memory before admitting any client request.
channel::channel(limits bounds, diagnostics::services sinks) : impl_(std::make_unique<impl>())
{
    if (!bounds.max_nodes || bounds.max_nodes > 65536 || !bounds.max_requests ||
        bounds.max_requests > 65536 || !bounds.max_text_bytes || bounds.max_text_bytes > 67'108'864)
    {
        throw std::invalid_argument("invalid accessibility limits");
    }

    impl_->bounds = bounds;
    impl_->sinks = std::move(sinks);
    impl_->requests.reserve(bounds.max_requests);
}

/// @brief Retained snapshots do not borrow this endpoint.
channel::~channel() = default;

/// @brief Validation/allocation failure preserves the old publication and pending commands.
void channel::publish(std::vector<node> nodes)
{
    impl_->check();
    const diagnostics::operation_scope trace{impl_->sinks, {"accessibility.publish", "ui.cpu"}};
    validate(nodes, impl_->bounds);
    auto prepared = std::make_shared<const snapshot>(std::move(nodes));
    std::lock_guard lock{impl_->mutex};
    if (impl_->closed)
    {
        throw std::logic_error("accessibility channel is closed");
    }

    impl_->current = std::move(prepared);
}

/// @brief Acquire owned immutable storage under a short lock.
std::shared_ptr<const snapshot> channel::read() const
{
    std::lock_guard lock{impl_->mutex};
    return impl_->current;
}

/// @brief Unsupported, stale, disabled and overflow requests do not evict admitted work.
bool channel::request(accessibility::request value)
{
    std::lock_guard lock{impl_->mutex};
    const auto* source = impl_->current ? impl_->current->find(value.target) : nullptr;
    if (!source || !supports(*source, value.operation) || impl_->closed ||
        impl_->requests.size() == impl_->bounds.max_requests)
    {
        return false;
    }

    impl_->requests.push_back(value);
    return true;
}

/// @brief Drop requests invalidated by intervening owner publications before dispatch.
std::vector<accessibility::request> channel::take_requests()
{
    impl_->check();
    const diagnostics::operation_scope trace{impl_->sinks, {"accessibility.drain", "ui.dispatch"}};
    std::lock_guard lock{impl_->mutex};
    std::vector<accessibility::request> output;
    output.reserve(impl_->requests.size());
    for (const auto& value : impl_->requests)
    {
        const auto* source = impl_->current ? impl_->current->find(value.target) : nullptr;
        if (source && supports(*source, value.operation))
        {
            output.push_back(value);
        }
    }

    impl_->requests.clear();
    return output;
}

/// @brief Disconnect atomically; queries retain old values but cannot execute old actions.
void channel::close()
{
    impl_->check();
    std::lock_guard lock{impl_->mutex};
    impl_->closed = true;
    impl_->current.reset();
    impl_->requests.clear();
}
} // namespace atlas::accessibility
