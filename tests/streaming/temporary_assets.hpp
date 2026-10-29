#pragma once

#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>

namespace atlas::streaming::tests
{
class temporary_assets
{
  public:
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
    ~temporary_assets()
    {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }
    temporary_assets(const temporary_assets&) = delete;
    temporary_assets& operator=(const temporary_assets&) = delete;

    [[nodiscard]] std::filesystem::path path() const
    {
        return directory_ / "menu.bin";
    }
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
    std::filesystem::path directory_;
};
} // namespace atlas::streaming::tests
