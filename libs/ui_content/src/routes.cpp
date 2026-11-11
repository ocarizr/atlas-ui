/** @file @brief Validate and compose authored model actions and Lua behavior routes. */
#include "routes.hpp"
#include "storage.hpp"
#include <atlas/ui_scripting/event_handler.hpp>

namespace atlas::ui_content::detail
{
namespace
{
/// @brief One input registration combines ordered action routes with an optional Lua response.
class composite_handler final : public ui::input_handler
{
  public:
    /// @brief Dispatch existing adapters on the owner and preserve the Lua routing response.
    ui::event_response on_event(const ui::routed_event& event,
                                ui::command_buffer& commands) override
    {
        for (const auto& action : actions)
        {
            (void)action->on_event(event, commands);
        }

        return script ? script->on_event(event, commands) : ui::event_response{};
    }

    std::vector<std::unique_ptr<ui_mvvm::action_handler>> actions; ///< Ordered model routes.
    std::unique_ptr<ui_scripting::event_handler> script; ///< One instance in the shared domain.
};

/// @brief Each unique Lua source is acquired once per candidate and registered by persistent ID.
scripting::instance_handle make_behavior(const behavior& spec, scripting::domain& domain,
                                         std::set<assets::asset_id>& registered,
                                         const construction& options)
{
    const auto name = assets::format_id(spec.source.id);
    if (registered.insert(spec.source.id).second)
    {
        const auto info = options.storage.describe(spec.source);
        const scripting::session_limits limits;
        auto text = acquire(info, limits.max_source_bytes, options);
        domain.register_source({name, info.revision, "@" + name + ".lua", std::move(text)});
    }

    return domain.create({name, spec.model});
}
} // namespace

/// @brief Initial domain setup executes through the existing director, one interpreter per UI.
std::shared_ptr<ui::input_handler> make_handler(const element& node, const model_map& models,
                                                const std::shared_ptr<scripting::domain>& domain,
                                                std::set<assets::asset_id>& registered,
                                                const construction& options)
{
    auto handler = std::make_shared<composite_handler>();
    handler->actions.reserve(node.actions.size());
    for (const auto& action : node.actions)
    {
        handler->actions.push_back(std::make_unique<ui_mvvm::action_handler>(
            models.at(action.model), std::span{&action.binding, 1}, options.diagnostics));
    }

    if (node.script)
    {
        const auto behavior = make_behavior(*node.script, *domain, registered, options);
        handler->script =
            std::make_unique<ui_scripting::event_handler>(domain, behavior, options.diagnostics);
    }

    return handler;
}
} // namespace atlas::ui_content::detail
