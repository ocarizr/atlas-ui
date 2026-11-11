/** @file @brief Memory-backed fixture exercises the actual streamer and model factory. */
#pragma once
#include <atlas/ui_content/instance.hpp>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>

namespace atlas::generated
{
/// @brief Compiled by the existing MVVM factory generator from the same authored model.
std::unique_ptr<mvvm::view_model>
make_ui_content_fixture(mvvm::runtime_limits limits,
                        std::shared_ptr<std::pmr::memory_resource> memory,
                        const diagnostics::services& sinks);
} // namespace atlas::generated

namespace atlas::ui_content::tests
{
/// @brief Give each test an independent mount and job pool with deterministic identity allocation.
class composition_fixture : public testing::Test
{
  protected:
    /// @brief Prepare sources through the typed streamer with the same references as the XML.
    void SetUp() override
    {
        assets::catalog_document draft;
        draft.space = assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23");
        ui_asset = add(draft, assets::asset_type::ui, "ui.xml", file(ATLAS_UI_DOCUMENT));
        vm_asset = add(draft, assets::asset_type::view_model, "hud.xml", file(ATLAS_UI_MODEL));
        script_asset =
            add(draft, assets::asset_type::script, "button.lua",
                "local xp = model.field('xp'); local ticks = 0; "
                "return {tick=function() ticks = ticks + 1; model.add_integer(xp,ticks) end,"
                "late_tick=function() model.request('load') end}");
        texture_asset = add(draft, assets::asset_type::texture, "icon.bin", "image");
        storage = std::make_unique<streaming::catalog_streamer>(
            jobs, assets::catalog::compile(std::move(draft)), std::move(backing));
    }

    /// @brief Production asset loading receives owned bytes, never fixture paths.
    static std::string file(const char* path)
    {
        std::ifstream input{path, std::ios::binary};
        if (!input)
        {
            throw std::runtime_error("Cannot open UI test fixture");
        }

        return {std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{}};
    }

    /// @brief Register one exact memory range and its durable source identity.
    assets::asset_ref add(assets::catalog_document& draft, assets::asset_type type,
                          const char* locator, const std::string& text)
    {
        const assets::asset_ref ref{assets::allocate_id(draft), type};
        draft.records.push_back({ref, {"fixture", locator}, 1, text.size()});
        backing.sources.emplace(
            locator, streaming::memory_asset{
                         std::make_shared<const streaming::asset_bytes>(text.begin(), text.end())});
        backing.routes.push_back({ref, 1, locator, 0});
        return ref;
    }

    jobs::job_system jobs{2};         ///< Storage workers retire before the pool.
    streaming::storage_mount backing; ///< Private memory sources before mount creation.
    std::unique_ptr<streaming::catalog_streamer> storage; ///< Exact typed asset boundary.
    assets::asset_ref ui_asset{};                         ///< Persisted UI source.
    assets::asset_ref vm_asset{};                         ///< Persisted ViewModel source.
    assets::asset_ref script_asset{};                     ///< Persisted Lua source.
    assets::asset_ref texture_asset{};                    ///< Persisted image source.
};
} // namespace atlas::ui_content::tests
