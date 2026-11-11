/** @file @brief Acquire authored UI, model and Lua bytes through the typed streamer. */
#include "storage.hpp"
#include "document_validation.hpp"

namespace atlas::ui_content::detail
{
/// @brief The owner waits only during explicit composition; worker delivery only moves bytes.
std::string acquire(assets::asset_info asset, std::size_t maximum, const construction& options)
{
    const diagnostics::operation_scope scope{options.diagnostics,
                                             {"ui.content.acquire", "ui.content"}};
    if (!asset.byte_size || asset.byte_size > maximum)
    {
        throw std::invalid_argument("Invalid authored asset length: " +
                                    assets::format_id(asset.reference.id));
    }

    std::optional<streaming::read_result> result;
    streaming::read_stamp stamp{};
    options.storage
        .read(
            {asset.reference, 0, static_cast<std::size_t>(asset.byte_size), asset.revision},
            [&](streaming::read_stamp completed, streaming::read_result data)
            {
                stamp = completed;
                result.emplace(std::move(data));
            },
            options.reads)
        .get();
    if (!result || !options.storage.accepts(stamp) || options.reads.cancellation.stop_requested())
    {
        if (options.reads.cancellation.stop_requested())
        {
            throw jobs::job_cancelled{};
        }

        throw std::runtime_error("Stale authored asset read: " +
                                 assets::format_id(asset.reference.id));
    }

    if (const auto* failure = std::get_if<std::exception_ptr>(&*result))
    {
        std::rethrow_exception(*failure);
    }

    const auto& bytes = *std::get<std::shared_ptr<const streaming::asset_bytes>>(*result);
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
} // namespace atlas::ui_content::detail

namespace atlas::ui_content
{
/// @brief Source context is a persistent ID; catalog paths never enter the document API.
document load(assets::asset_ref source, const construction& options)
{
    if (source.type != assets::asset_type::ui)
    {
        throw std::invalid_argument("UI document load requires a UI asset");
    }

    const auto name = assets::format_id(source.id);
    const auto text =
        detail::acquire(options.storage.describe(source), detail::max_document_bytes, options);
    return parse_xml({text, name}, options.diagnostics);
}
} // namespace atlas::ui_content
