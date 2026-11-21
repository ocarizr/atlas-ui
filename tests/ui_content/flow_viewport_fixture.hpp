/** @file @brief Typed controlled sources for shared viewport routing and history tests. */
#pragma once
#include "flow_runtime_fixture.hpp"
#include <atlas/ui_content/flow_viewport.hpp>
#include <map>

namespace atlas::ui_content::tests
{
/// @brief Controlled sources isolate routing from storage timing.
class flow_viewport_test : public flow_runtime_test
{
  protected:
    /// @brief Compose typed views and actual semantic bridges with no disk dependency.
    class typed_load final : public flow_load
    {
      public:
        /// @brief Retain immutable authored values through private owner composition.
        typed_load(construction options, flow_load_request request, document source)
            : options_{std::move(options)}, request_{std::move(request)}, source_{std::move(source)}
        {
        }

        std::unique_ptr<instance> poll() override
        {
            options_.navigate = request_.dispatch;
            options_.navigation = request_.controls;
            return instance::build(source_, options_);
        } ///< Mirror the production loader's route installation.
      private:
        construction options_;      ///< Borrow fixture storage.
        flow_load_request request_; ///< Own route span through composition.
        document source_;           ///< Deterministic authored source.
    };

    std::map<std::uint64_t, document> documents; ///< Optional nested/typed UI source fixtures.

    flow_loader typed_loader()
    {
        return [this](flow_load_request request)
        {
            auto source = documents.contains(request.source.id.local)
                              ? documents.at(request.source.id.local)
                              : parse_xml({screen_xml});
            source.presentation.type = request.source.type;
            return std::make_unique<typed_load>(construction{storage}, std::move(request),
                                                std::move(source));
        };
    } ///< Exact same immutable authored documents per load.

    /// @brief One small popup above a full viewport menu, with optional lower input.
    flow_document popup_graph(bool modal = true, bool outside = true)
    {
        flow_document result;
        result.regions = {{"main", "home", ""}};
        result.screens = {{"home", "main", asset(1)},
                          {"popup", "main", asset(2, assets::asset_type::popup)}};
        result.screens.back().popup = {modal, outside, true};
        result.transitions = {{"open", "home", "popup", flow_operation::overlay},
                              {"close", "popup", {}, flow_operation::close_popup},
                              {"reload", "home", {}, flow_operation::reload}};
        return result;
    }
};

} // namespace atlas::ui_content::tests
