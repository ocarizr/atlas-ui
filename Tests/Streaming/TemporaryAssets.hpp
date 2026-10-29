#pragma once

#include <Windows.h>
#include <objbase.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace atlas::streaming::tests
{
class TemporaryAssets
{
  public:
    TemporaryAssets()
    {
        GUID id{};
        wchar_t text[40]{};
        if (FAILED(CoCreateGuid(&id)) || StringFromGUID2(id, text, 40) == 0)
        {
            throw std::runtime_error{"Cannot create a temporary asset identity"};
        }
        directory_ = std::filesystem::temp_directory_path() / text;
        std::filesystem::create_directory(directory_);
    }
    ~TemporaryAssets()
    {
        std::error_code error;
        std::filesystem::remove_all(directory_, error);
    }
    TemporaryAssets(const TemporaryAssets&) = delete;
    TemporaryAssets& operator=(const TemporaryAssets&) = delete;

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
