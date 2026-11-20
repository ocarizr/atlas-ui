/** @file @brief Runtime topology/catalog validation remains separate from editable drafts. */
#include "owner_scope.hpp"
#include <atlas/editor_flow/session.hpp>

namespace atlas::editor_flow
{
/// @brief Reject an uninitialized candidate instead of returning a dangling graph reference.
const ui_content::flow_document& runtime_candidate::graph() const
{
    if (!graph_)
    {
        throw std::logic_error("Uninitialized Flow runtime candidate");
    }

    return *graph_;
}

/// @brief Runtime serialization is the existing strict Flow contract, never authoring XML.
std::string runtime_candidate::xml() const
{
    return ui_content::write_flow_xml(graph());
}

/// @brief Preflight the graph and optional catalog before issuing a private publication token.
runtime_candidate session::compile(const asset_check& assets) const
{
    check();
    const diagnostics::operation_scope scope{sinks_, {"editor.flow.compile", "editor.flow"}};

    const detail::owner_scope guard{busy_};
    try
    {
        (void)ui_content::write_flow_xml(current_.source->graph);
        if (assets)
        {
            for (const auto& screen : current_.source->graph.screens)
            {
                if (!assets(screen.asset))
                {
                    throw std::invalid_argument("Missing Flow draft asset: " + screen.id);
                }
            }
        }

        runtime_candidate result;
        result.graph_ = std::make_shared<const ui_content::flow_document>(current_.source->graph);
        result.assets_ = assets;
        result.owner_ = identity_;
        result.revision_ = current_.id;
        return result;
    }
    catch (...)
    {
        diagnostics::write(sinks_, diagnostics::log_level::warning, "editor.flow",
                           "Flow candidate rejected; published runtime preserved");
        throw;
    }
}
} // namespace atlas::editor_flow
