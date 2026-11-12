/** @file @brief Windows editor file metadata and durable sibling-temp replacement. */
#include "document_file.hpp"
#include <Windows.h>
#include <atomic>
#include <optional>
#include <system_error>

namespace atlas::editor
{
namespace
{
/// @brief Surface native errors while preserving the affected operation.
[[noreturn]] void fail(const char* operation)
{
    throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), operation);
}

/// @brief Probe metadata for streamer registration, distinguishing missing from inaccessible.
std::optional<std::uint64_t> file_length(const std::filesystem::path& path)
{
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
    {
        if (const auto error = GetLastError();
            error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        {
            return std::nullopt;
        }

        fail("Inspect UI document");
    }

    if ((info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
    {
        throw std::invalid_argument("Editor document path names a directory");
    }

    return (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32U) | info.nFileSizeLow;
}

/// @brief A uniquely created sibling file is removed unless its rename succeeds.
class pending_file
{
  public:
    /// @brief Reserve a temporary file without truncating a file owned by another process.
    explicit pending_file(const std::filesystem::path& destination)
    {
        static std::atomic<std::uint64_t> next{1};
        for (unsigned attempt = 0; attempt < 32; ++attempt)
        {
            path_ = destination;
            path_ += L".atlas-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                     std::to_wstring(next.fetch_add(1, std::memory_order_relaxed)) + L".tmp";
            handle_ = CreateFileW(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle_ != INVALID_HANDLE_VALUE)
            {
                return;
            }

            if (GetLastError() != ERROR_FILE_EXISTS)
            {
                fail("Create temporary UI document");
            }
        }

        throw std::runtime_error("Could not reserve a temporary UI document");
    }

    /// @brief Close and remove only the candidate owned by this save.
    ~pending_file()
    {
        if (handle_ != INVALID_HANDLE_VALUE)
        {
            CloseHandle(handle_);
        }

        if (!path_.empty())
        {
            DeleteFileW(path_.c_str());
        }
    }

    pending_file(const pending_file&) = delete;            ///< Single native handle owner.
    pending_file& operator=(const pending_file&) = delete; ///< Single temporary path owner.

    /// @brief Flush complete XML before a same-directory atomic replacement.
    void publish(std::string_view bytes, const std::filesystem::path& destination)
    {
        DWORD written = 0;
        if (!WriteFile(handle_, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr))
        {
            fail("Write UI document");
        }

        if (written != bytes.size())
        {
            throw std::runtime_error("Incomplete UI document write");
        }

        if (!FlushFileBuffers(handle_))
        {
            fail("Flush UI document");
        }

        if (!CloseHandle(handle_))
        {
            fail("Close UI document");
        }

        handle_ = INVALID_HANDLE_VALUE;
        if (!MoveFileExW(path_.c_str(), destination.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            fail("Replace UI document");
        }

        path_.clear();
    }

  private:
    std::filesystem::path path_;           ///< Exclusively created sibling.
    HANDLE handle_ = INVALID_HANDLE_VALUE; ///< Native write handle until publication.
};
} // namespace

/// @brief Resolve the real executable path independently of the working directory.
std::filesystem::path installed_tools_root()
{
    std::wstring path(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        fail("Locate installed editor tools");
    }

    path.resize(length);
    return std::filesystem::path{path}.parent_path() / "content" / "editor_tools";
}

/// @brief Missing files start a fresh editor document.
bool document_exists(const std::filesystem::path& path)
{
    return file_length(path).has_value();
}

/// @brief Register exactly the selected document, then delegate byte acquisition to the streamer.
std::string read_editor_source(const std::filesystem::path& path, jobs::job_system& jobs,
                               const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"editor.open", "editor"}};
    const auto size = file_length(path);
    if (!size || *size == 0 || *size > std::uint64_t{4} * 1024 * 1024)
    {
        throw std::invalid_argument("Missing, empty or oversized UI document");
    }

    streaming::asset_streamer storage{
        jobs,
        {{"source", streaming::loose_file_asset{std::filesystem::absolute(path)}}},
        {},
        {},
        sinks};
    std::string bytes;
    storage
        .read({"source", 0, static_cast<std::size_t>(*size)},
              [&](streaming::read_result result)
              {
                  if (const auto* failure = std::get_if<std::exception_ptr>(&result))
                  {
                      std::rethrow_exception(*failure);
                  }

                  const auto& buffer =
                      *std::get<std::shared_ptr<const streaming::asset_bytes>>(result);
                  bytes.assign(reinterpret_cast<const char*>(buffer.data()), buffer.size());
              })
        .get();
    return bytes;
}

/// @brief Parse authoring bytes with the source filename in validation errors.
ui_content::document open_document(const std::filesystem::path& path, jobs::job_system& jobs,
                                   const diagnostics::services& sinks)
{
    const auto bytes = read_editor_source(path, jobs, sinks);
    const auto name = path.filename().string();
    return ui_content::parse_xml({bytes, name}, sinks);
}

/// @brief Persist canonical authoring values without holding a preview or touching the engine tree.
void save_document(const std::filesystem::path& path, const ui_content::document& source,
                   const diagnostics::services& sinks)
{
    const diagnostics::operation_scope scope{sinks, {"editor.save", "editor"}};
    const auto bytes = ui_content::write_xml(source, sinks);
    pending_file candidate{path};
    candidate.publish(bytes, path);
}
} // namespace atlas::editor
