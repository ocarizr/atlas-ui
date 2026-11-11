/** @file @brief Resolve exact ViewModel asset revisions via XML or prepared factories. */
#include "model_sources.hpp"
#include "document_validation.hpp"
#include "storage.hpp"
#include <atlas/mvvm_content/document.hpp>

namespace atlas::ui_content::detail
{
namespace
{
using factory_map = std::map<assets::asset_id, prepared_model>; ///< Indexed prepared candidates.

/// @brief Reject invalid prepared tables before creating model or script state.
factory_map index_factories(const construction& options)
{
    if (options.factories.size() > max_models)
    {
        throw std::invalid_argument("Too many prepared UI models");
    }

    factory_map factories;
    for (const auto& entry : options.factories)
    {
        if (entry.source.type != assets::asset_type::view_model || !entry.make || !entry.revision ||
            !factories.emplace(entry.source.id, entry).second)
        {
            throw std::invalid_argument("Invalid or duplicate prepared UI model");
        }
    }

    return factories;
}

/// @brief Resolve the model type through the mounted catalog, even for native factories.
std::shared_ptr<mvvm::view_model> make_model(const model_context& context,
                                             const construction& options,
                                             const factory_map& factories,
                                             std::map<assets::asset_id, mvvm::schema>& parsed)
{
    const auto info = options.storage.describe(context.source);
    const auto factory = factories.find(context.source.id);
    if (factory != factories.end())
    {
        if (factory->second.revision != info.revision)
        {
            throw std::invalid_argument("Stale prepared ViewModel revision");
        }

        auto model = factory->second.make({}, options.memory, options.diagnostics);
        if (!model || model->inspect().frame != 0 || model->inspect().failed)
        {
            throw std::invalid_argument(
                "Prepared ViewModel factory did not return a fresh instance");
        }

        return model;
    }

    auto schema = parsed.find(info.reference.id);
    if (schema == parsed.end())
    {
        const mvvm_content::content_limits limits;
        const auto bytes = acquire(info, limits.max_bytes, options);
        const auto definition = mvvm_content::parse_xml(bytes, limits, options.diagnostics);
        schema = parsed.emplace(info.reference.id, mvvm::compile(definition, limits.schema)).first;
    }

    return std::make_shared<mvvm::view_model>(schema->second, mvvm::runtime_limits{},
                                              options.memory, options.diagnostics);
}
} // namespace

/// @brief Wrap source failures with both the local context and the persistent ID.
model_map make_models(const document& source, const construction& options)
{
    model_map models;
    std::map<assets::asset_id, mvvm::schema> parsed;
    const auto factories = index_factories(options);
    for (const auto& context : source.models)
    {
        try
        {
            models.emplace(context.name, make_model(context, options, factories, parsed));
        }
        catch (const std::logic_error& error)
        {
            throw std::invalid_argument("UI model " + context.name + " (" +
                                        assets::format_id(context.source.id) +
                                        "): " + error.what());
        }
    }

    return models;
}
} // namespace atlas::ui_content::detail
