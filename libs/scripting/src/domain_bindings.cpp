/** @file @brief Immutable per-generation source and project-service capability snapshots. */
#include "domain_state.hpp"
#include <stdexcept>

namespace atlas::scripting
{
/// @brief Copy catalog metadata while sharing immutable owned source bytes between generations.
std::shared_ptr<const detail::execution_bindings> domain::impl::snapshot() const
{
    auto catalog = sources;
    auto bindings = std::make_shared<detail::execution_bindings>();
    bindings->source = [catalog](std::string_view identity)
    {
        const auto found = catalog->find(identity);
        if (found == catalog->end())
        {
            throw std::invalid_argument("unknown catalog module identity");
        }

        return detail::module_source{found->second->text, found->second->source_name};
    };
    auto retained_services = services;
    bindings->service = [retained_services, destinations = destinations](
                            std::string_view name, std::span<const mvvm::value> arguments)
    {
        const auto found = retained_services->find(name);
        if (found == retained_services->end())
        {
            throw std::invalid_argument("unknown synchronous service");
        }

        const auto& service = found->second;
        if (arguments.size() != service.parameters.size())
        {
            throw std::invalid_argument("service argument count mismatch");
        }

        for (std::size_t i = 0; i < arguments.size(); ++i)
        {
            if (mvvm::type_of(arguments[i]) != service.parameters[i])
            {
                throw std::invalid_argument("service argument type mismatch");
            }
        }

        const diagnostics::operation_scope scope{destinations,
                                                 {"script.service.call", "script.cpu"}};
        auto result = service.implementation->invoke(arguments);
        if (result.size() != service.results.size())
        {
            throw std::invalid_argument("service result count mismatch");
        }

        for (std::size_t i = 0; i < result.size(); ++i)
        {
            if (mvvm::type_of(result[i]) != service.results[i])
            {
                throw std::invalid_argument("service result type mismatch");
            }
        }

        return result;
    };
    return bindings;
}

/// @brief Registration supplies no engine-specific simulation or application authority.
void domain::register_service(service_definition service)
{
    impl_->check();
    mvvm::validate_text(service.name, 128);
    if (service.name.empty() || !service.implementation || service.parameters.size() > 32 ||
        service.results.size() > 32 || impl_->services->size() >= 64 ||
        impl_->services->contains(service.name))
    {
        throw std::invalid_argument("invalid or duplicate synchronous service");
    }

    const auto name = service.name;
    auto next = std::make_shared<impl::service_catalog>(*impl_->services);
    next->emplace(name, std::move(service));
    impl_->services = std::move(next);
}
} // namespace atlas::scripting
