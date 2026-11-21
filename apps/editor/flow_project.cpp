/** @file @brief Streamer-only acquisition on explicit graph open/discovery, never CPU workers. */
#include "flow_project.hpp"
#include "document_file.hpp"
#include "flow_file.hpp"
#include <algorithm>
#include <set>

namespace atlas::editor
{
/// @brief Source and recovery remain separate even when reopening after a malformed source write.
std::unique_ptr<editor_flow::persistence> open_flow_document(const std::filesystem::path& path,
                                                             jobs::job_system& jobs,
                                                             const diagnostics::services& sinks,
                                                             bool recover)
{
    if (recover || document_exists(path))
    {
        return std::make_unique<editor_flow::persistence>(path, make_flow_storage(jobs, sinks),
                                                          recover, sinks);
    }

    editor_flow::document initial;
    initial.id = "flow";
    initial.graph.regions.push_back({"viewport", "", ""});
    return std::make_unique<editor_flow::persistence>(path, std::move(initial),
                                                      make_flow_storage(jobs, sinks), sinks, true);
}

/// @brief Read a bounded script closure through typed streaming before isolated registry discovery.
void load_flow_module(editor_view& view, assets::asset_ref entry, const assets::catalog& catalog,
                      streaming::catalog_streamer& storage)
{
    const auto& records = catalog.document().records;
    std::vector<assets::asset_ref> pending{entry};
    std::set<assets::asset_ref> visited;
    std::vector<scripting::source_asset> sources;
    while (!pending.empty())
    {
        const auto reference = pending.back();
        pending.pop_back();
        if (!visited.insert(reference).second)
        {
            continue;
        }

        if (visited.size() > 64 || reference.type != assets::asset_type::script)
        {
            throw std::invalid_argument("Callable discovery requires at most 64 script sources");
        }

        const auto record = std::ranges::find(records, reference, &assets::asset_record::reference);
        if (record == records.end())
        {
            throw std::invalid_argument("Missing callable module catalog dependency");
        }

        const auto info = storage.describe(reference);
        if (!info.byte_size || info.byte_size > scripting::session_limits{}.max_source_bytes)
        {
            throw std::length_error("Callable source exceeds the Lua source budget");
        }

        std::optional<streaming::read_result> result;
        streaming::read_stamp stamp{};
        storage
            .read({reference, 0, static_cast<std::size_t>(info.byte_size), info.revision},
                  [&](auto completed, auto data)
                  {
                      stamp = completed;
                      result.emplace(std::move(data));
                  })
            .get();
        if (!result || !storage.accepts(stamp))
        {
            throw std::runtime_error("Stale callable discovery read");
        }

        if (const auto* failure = std::get_if<std::exception_ptr>(&*result))
        {
            std::rethrow_exception(*failure);
        }

        const auto& bytes = *std::get<std::shared_ptr<const streaming::asset_bytes>>(*result);
        sources.push_back({assets::format_id(reference.id),
                           info.revision,
                           "@" + record->source.path,
                           {reinterpret_cast<const char*>(bytes.data()), bytes.size()}});
        pending.insert(pending.end(), record->dependencies.begin(), record->dependencies.end());
    }

    view.bind_flow_module(sources, {assets::format_id(entry.id), {"project"}});
}
} // namespace atlas::editor
