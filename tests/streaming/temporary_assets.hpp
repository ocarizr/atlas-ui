/**
 * @file
 * @brief Behavioral coverage for temporary assets with isolated scenario ownership and cleanup.
 */

#pragma once

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>

namespace atlas::streaming::tests
{
/// @brief Scenario state for temporary assets, isolated per test and released through RAII.
class temporary_assets
{
  public:
    /// @brief Create an isolated temporary asset directory for this test.
    temporary_assets()
    {
        std::random_device random;
        const auto base = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 16; ++attempt)
        {
            const auto name =
                "atlas_streaming_" + std::to_string(random()) + "_" + std::to_string(random());
            const auto candidate = base / name;
            if (std::filesystem::create_directory(candidate))
            {
                directory_ = candidate;
                return;
            }
        }

        throw std::runtime_error{"Cannot create an isolated temporary asset directory"};
    }

    /// @brief Release temporary assets resources and complete its scoped cleanup.
    ~temporary_assets()
    {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }

    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    temporary_assets(const temporary_assets&) = delete;
    /// @brief Disable copy construction/assignment to preserve unique lifecycle ownership.
    temporary_assets& operator=(const temporary_assets&) = delete;

    /// @brief Return this fixture's isolated temporary path.
    [[nodiscard]] std::filesystem::path path() const
    {
        return directory_ / "menu.bin";
    }

    /// @brief Write scenario bytes into this fixture's isolated storage.
    void write() const
    {
        std::ofstream file{path(), std::ios::binary};
        file << "ABCD";
        if (!file)
        {
            throw std::runtime_error{"Cannot prepare the test asset"};
        }
    }

  private:
    /// @brief Scenario-owned directory used to observe behavior and cleanup without sharing mutable
    /// state between tests.
    std::filesystem::path directory_;
};
} // namespace atlas::streaming::tests
