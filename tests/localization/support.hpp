/** @file @brief Shared reproducible localization fixtures and deterministic injected rules. */
#pragma once
#include <atlas/localization/context.hpp>
#include <fstream>
#include <gtest/gtest.h>
#include <iterator>

namespace atlas::localization::tests
{
/// @brief Read an isolated immutable repository fixture; production never opens this file.
inline definition fixture()
{
    std::ifstream file{std::string{ATLAS_LOCALIZATION_DATA_DIR} + "/strings.xml", std::ios::binary};
    if (!file)
    {
        throw std::runtime_error("missing localization test fixture");
    }

    return parse_xml(
        std::string{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}});
}

/// @brief Explicit fake is not a claim that English rules implement every language.
inline plural_category fake_rule(std::string_view, double count)
{
    return count == 1 ? plural_category::one : plural_category::other;
}

/// @brief Each test owns a fresh catalog, revision and observer list.
class context_fixture : public ::testing::Test
{
  protected:
    context source{fixture(), fake_rule}; ///< Per-test single owner context.
};
} // namespace atlas::localization::tests
