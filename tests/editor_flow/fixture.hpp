/** @file @brief Small independent graph documents and transactional storage per test. */
#pragma once
#include <atlas/editor_flow/persistence.hpp>
#include <gtest/gtest.h>
#include <map>

namespace atlas::editor_flow::tests
{
/// @brief Fixed IDs make source and runtime equivalence reproducible.
inline document example()
{
    document result;
    result.id = "settings_flow";
    const assets::asset_ref asset{{assets::parse_namespace("018d0b95cf2673c6a0d7dd8ce5d29b23"), 1},
                                  assets::asset_type::menu};
    result.graph.regions = {{"viewport", "home", ""}};
    result.graph.screens = {{"home", "viewport", asset}};
    result.layout = {{{item_kind::screen, "home"}, "Home", 10, 20}};
    result.selection = {item_kind::screen, "home"};
    return result;
}

/// @brief Model atomic replacement and explicit failure without mutating shared global state.
class memory_storage
{
  public:
    /// @brief The fixture outlives each returned adapter and owned persistence session.
    storage adapter()
    {
        return {[this](const auto& path) -> std::optional<std::string>
                {
                    const auto found = files.find(path);
                    return found == files.end() ? std::nullopt : std::optional{found->second};
                },
                [this](const auto& path, std::string_view bytes)
                {
                    if (fail_write)
                    {
                        throw std::runtime_error("Interrupted atomic replacement");
                    }

                    files[path] = bytes;
                },
                [this](const auto& path)
                {
                    if (fail_remove)
                    {
                        throw std::runtime_error("Recovery cleanup failed");
                    }

                    files.erase(path);
                }};
    }

    std::map<std::filesystem::path, std::string> files; ///< Committed source/checkpoint bytes only.
    bool fail_write = false;  ///< Never expose a partially written destination.
    bool fail_remove = false; ///< Keep the old recovery available for retry.
};
} // namespace atlas::editor_flow::tests
