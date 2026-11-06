/** @file @brief Transactional producer batches restore exact prior admission on any error. */
#include "runtime_state.hpp"

namespace atlas::mvvm
{
/// @brief Route one typed demand through the same checks as direct native producers.
void view_model::impl::admit(const command& demand)
{
    if (const auto* field = std::get_if<field_handle>(&demand.target))
    {
        if (demand.arguments.size() != 1)
        {
            throw std::invalid_argument("MVVM field command requires one argument");
        }

        if (demand.update == update_kind::assign)
        {
            facade->set(*field, demand.arguments.front());
        }
        else if (demand.update == update_kind::add_integer)
        {
            if (type_of(demand.arguments.front()) != value_type::integer)
            {
                throw std::invalid_argument("MVVM integer delta type mismatch");
            }

            facade->add_integer(*field, std::get<std::int64_t>(demand.arguments.front()));
        }
        else if (demand.update == update_kind::add_number)
        {
            if (type_of(demand.arguments.front()) != value_type::number)
            {
                throw std::invalid_argument("MVVM number delta type mismatch");
            }

            facade->add_number(*field, std::get<double>(demand.arguments.front()));
        }
        else
        {
            throw std::invalid_argument("invalid MVVM update kind");
        }
    }
    else
    {
        if (demand.update != update_kind::assign)
        {
            throw std::invalid_argument("relative MVVM signal command");
        }

        if (const auto* action = std::get_if<action_handle>(&demand.target))
        {
            facade->invoke(*action, demand.arguments);
        }
        else
        {
            facade->emit(std::get<event_handle>(demand.target), demand.arguments);
        }
    }
}

/// @brief Checkpoint only transport metadata; observers and committed typed columns never change.
void view_model::submit(std::span<const command> commands)
{
    impl_->writable();
    if (commands.empty())
    {
        return;
    }

    auto* resource = impl_->memory.get();
    std::pmr::vector<queued_command> queue{resource};
    std::pmr::vector<value> arguments{resource};
    std::pmr::vector<pending_field> latest{resource};
    {
        const detail::flag_guard guard{impl_->admitting};
        queue = impl_->queue;
        arguments = impl_->arguments;
        latest = impl_->latest;
    }

    const auto stats = impl_->stats;
    const auto payload = impl_->payload_bytes;
    const auto admitted = impl_->admitted_arguments;
    const auto segment = impl_->segment;
    try
    {
        for (const auto& demand : commands)
        {
            impl_->admit(demand);
        }
    }
    catch (...)
    {
        // Keep arena addresses stable for a caller borrowing a notification span during flush.
        impl_->queue.assign(queue.begin(), queue.end());
        impl_->arguments.assign(std::make_move_iterator(arguments.begin()),
                                std::make_move_iterator(arguments.end()));
        impl_->latest.assign(latest.begin(), latest.end());
        impl_->stats = stats;
        impl_->payload_bytes = payload;
        impl_->admitted_arguments = admitted;
        impl_->segment = segment;
        throw;
    }
}
} // namespace atlas::mvvm
