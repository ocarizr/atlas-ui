/** @file @brief Native file selection grants explicit import and destination paths. */
#include "asset_files.hpp"
#include <Windows.h>
#include <commdlg.h>

namespace atlas::editor
{
/// @brief Native selection is a host affordance; asset rows and actions remain Atlas UI.
std::optional<std::filesystem::path> choose_asset_file(const std::filesystem::path& root,
                                                       assets::asset_type type, bool destination)
{
    std::wstring buffer(32768, L'\0');
    const auto initial = root / std::string{assets::format_type(type)};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetActiveWindow();
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = static_cast<DWORD>(buffer.size());
    dialog.lpstrInitialDir = initial.c_str();
    dialog.lpstrFilter = L"Asset source files\0*.*\0\0";
    dialog.Flags = OFN_NOCHANGEDIR | OFN_DONTADDTORECENT | OFN_PATHMUSTEXIST |
                   (destination ? OFN_NOTESTFILECREATE : OFN_FILEMUSTEXIST);
    const bool accepted =
        destination ? GetSaveFileNameW(&dialog) != 0 : GetOpenFileNameW(&dialog) != 0;
    if (!accepted)
    {
        if (CommDlgExtendedError() != 0)
        {
            throw std::runtime_error("Editor asset file selection failed");
        }

        return {};
    }

    return std::filesystem::path{buffer.c_str()};
}
} // namespace atlas::editor
