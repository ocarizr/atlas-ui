/** @file @brief Public owner-thread Flow Manager and deferred navigation commands. */
#include "flow_state.hpp"
#include <algorithm>
#include <cmath>

namespace atlas::ui_content
{
namespace
{
/// @brief Keep external lifecycle callbacks from reentering a publication.
class sync_guard
{
  public:
    explicit sync_guard(bool& busy) : busy_{busy}
    {
        busy_ = true;
    } ///< Open a sync operation.

    ~sync_guard()
    {
        busy_ = false;
    } ///< Restore admission on scope exit.

    sync_guard(const sync_guard&) = delete;            ///< Never copy an active guard.
    sync_guard& operator=(const sync_guard&) = delete; ///< Never copy an active guard.
  private:
    bool& busy_; ///< Owner's reentrancy flag.
};
} // namespace

/// @brief Validate topology before admitting the first navigation command.
flow_manager::impl::impl(flow_document source, flow_loader factory, diagnostics::services sinks)
    : graph{std::move(source)}, loader{std::move(factory)}, diagnostics{std::move(sinks)},
      admission{std::make_shared<detail::flow_admission>()}, current(graph.regions.size())
{
    validate_flow(graph);
    if (!loader)
    {
        throw std::invalid_argument("Flow needs a screen loader");
    }

    for (const auto& value : graph.regions)
    {
        if (value.owner.empty())
        {
            root = region(value.id);
        }
    }

    for (const auto& value : graph.parameters)
    {
        parameters.push_back(value.value);
    }
}

/// @brief Navigation is an owner service with a single publication at a time.
void flow_manager::impl::check() const
{
    if (admission->owner != std::this_thread::get_id() || busy || closed)
    {
        throw std::logic_error("Flow requires its active owner outside publication");
    }
}

/// @brief Retain graph and load policy in one independent viewport owner.
flow_manager::flow_manager(flow_document graph, flow_loader loader, diagnostics::services sinks)
    : impl_{std::make_unique<impl>(std::move(graph), std::move(loader), std::move(sinks))}
{
}

flow_manager::~flow_manager() = default; ///< Discard pending work and release owner views.

/// @brief Queue initialization for the next owner publication point.
void flow_manager::start()
{
    impl_->check();
    if (!impl_->current.at(impl_->root).empty() || impl_->admission->command || impl_->candidate)
    {
        throw std::logic_error("Flow already has a viewport or pending request");
    }

    impl_->admission->command.emplace();
}

/// @brief Admit only one explicit action before the next sync point.
bool flow_manager::request(std::string_view transition)
{
    impl_->check();
    auto& admission = *impl_->admission;
    if (admission.busy || admission.command || transition.empty() || transition.size() > 64)
    {
        return false;
    }

    admission.command = detail::flow_command{std::string{transition}, 0};
    return true;
}

/// @brief Copy an exact host value while keeping authored parameter types immutable.
void flow_manager::set_parameter(std::string_view name, mvvm::value value)
{
    impl_->check();
    const auto& parameters = impl_->graph.parameters;
    const auto found = std::ranges::find(parameters, name, &flow_parameter::id);
    if (found == parameters.end() || mvvm::type_of(found->value) != mvvm::type_of(value))
    {
        throw std::invalid_argument("Unknown flow parameter or mismatched type");
    }

    if (const auto* number = std::get_if<double>(&value); number && !std::isfinite(*number))
    {
        throw std::invalid_argument("Nonfinite flow parameter");
    }

    if (const auto* text = std::get_if<std::string>(&value))
    {
        mvvm::validate_text(*text, 4096);
    }

    impl_->parameters.at(static_cast<std::size_t>(found - parameters.begin())) = std::move(value);
}

/// @brief Cancel unpublished state and reopen the command slot.
void flow_manager::cancel()
{
    impl_->check();
    impl_->clear_candidate();
}

/// @brief Observe completion and publish at an explicit owner frame boundary.
flow_status flow_manager::advance(std::uint64_t frame)
{
    impl_->check();
    const sync_guard sync{impl_->busy};
    impl_->check_frame(frame);
    impl_->frame = frame;
    if (!impl_->candidate && !impl_->admission->command)
    {
        return flow_status::idle;
    }

    const diagnostics::operation_scope scope{impl_->diagnostics, {"ui.flow.advance", "ui.content"}};
    impl_->failure = {};
    try
    {
        impl_->admission->busy = true;
        if (auto& command = impl_->admission->command; command)
        {
            auto next = std::move(*command);
            command.reset();
            if (!impl_->plan(next))
            {
                impl_->clear_candidate();
                return flow_status::rejected;
            }
        }

        const auto& candidate = impl_->candidate;
        if (candidate && candidate->edge && !impl_->enabled(*candidate->edge))
        {
            impl_->clear_candidate();
            return flow_status::rejected;
        }

        if (!impl_->poll())
        {
            return flow_status::loading;
        }

        impl_->publish(frame);
        impl_->clear_candidate();
        return flow_status::committed;
    }
    catch (...)
    {
        impl_->failure = std::current_exception();
        impl_->clear_candidate();
        return flow_status::failed;
    }
}

/// @brief Expose the last asynchronous load or cleanup error without throwing it again.
std::exception_ptr flow_manager::failure() const
{
    impl_->check();
    return impl_->failure;
}

/// @brief Borrow the published list in painter order.
std::vector<flow_presentation> flow_manager::presentation() const
{
    impl_->check();
    std::vector<flow_presentation> result;
    for (const auto& position : impl_->visible(impl_->current))
    {
        const auto& entry = impl_->current.at(position.region).at(position.index);
        if (entry.session)
        {
            result.push_back({impl_->graph.regions.at(position.region).id,
                              impl_->graph.screens.at(entry.screen).id, entry.session->view.get(),
                              position.input});
        }
    }

    return result;
}

namespace
{
/// @brief Resolve a focus bookmark only if it still participates in the current presentation.
bool restore(instance& view, const std::string& name)
{
    if (name.empty())
    {
        return false;
    }

    ui::element_handle target;
    try
    {
        target = view.find(name);
    }
    catch (const std::out_of_range&)
    {
        return false;
    }

    if (!view.input().can_focus(target))
    {
        return false;
    }

    view.input().focus(target);
    return true;
}
} // namespace

/// @brief Restore a remembered authored focus ID or the menu's default when usable.
void flow_manager::restore_focus(instance& view)
{
    impl_->check();
    const sync_guard sync{impl_->busy};
    for (const auto& position : impl_->visible(impl_->current))
    {
        auto& entry = impl_->current.at(position.region).at(position.index);
        if (position.input && entry.session && entry.session->view.get() == &view &&
            entry.focus_pending)
        {
            if (!restore(view, entry.focus) &&
                !restore(view, impl_->graph.screens.at(entry.screen).focus))
            {
                view.input().focus({});
            }

            entry.focus_pending = false;
            return;
        }
    }
}

/// @brief Cancel outstanding requests and deliver terminal lifecycle to all live screens.
void flow_manager::shutdown(std::uint64_t frame)
{
    impl_->check();
    const sync_guard sync{impl_->busy};
    impl_->check_frame(frame);
    impl_->clear_candidate();
    impl_->failure = {};
    try
    {
        impl_->candidate.emplace(
            detail::flow_candidate{detail::flow_stacks(impl_->current.size()), {}, nullptr});
        impl_->admission->busy = true;
        impl_->publish(frame);
    }
    catch (...)
    {
        impl_->clear_candidate();
        throw;
    }

    impl_->clear_candidate();
    impl_->closed = true;
    if (impl_->failure)
    {
        std::rethrow_exception(impl_->failure);
    }
}
} // namespace atlas::ui_content
