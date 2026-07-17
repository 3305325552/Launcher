#include "ui/MainDockWin32.hpp"

#include "core/StringEncoding.hpp"
#include "launcher/AppIdentity.hpp"

#include <commdlg.h>
#include <imgui.h>
#include <filesystem>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>

namespace launcher {
namespace {

std::string fileDialog(const wchar_t* title, const wchar_t* filter, bool save)
{
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dialogOwnerHandle();
    ofn.lpstrTitle = title;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_ENABLESIZING | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!save) {
        ofn.Flags |= OFN_FILEMUSTEXIST;
    } else {
        ofn.Flags |= OFN_OVERWRITEPROMPT;
    }
    bringWindowToDialogFront(ofn.hwndOwner);
    const BOOL accepted = save ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
    bringWindowToDialogFront(ofn.hwndOwner);
    if (!accepted) {
        return {};
    }
    return narrow(file);
}

} // namespace

std::filesystem::path appFolder()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

HWND mainWindowHandle()
{
    if (ImGuiViewport* viewport = ImGui::GetMainViewport()) {
        if (auto* hwnd = static_cast<HWND>(viewport->PlatformHandleRaw)) {
            return hwnd;
        }
    }
    return FindWindowW(app_identity::kWindowClassName, nullptr);
}

HWND dialogOwnerHandle()
{
    if (ImGuiViewport* viewport = ImGui::GetWindowViewport()) {
        if (auto* hwnd = static_cast<HWND>(viewport->PlatformHandleRaw)) {
            return hwnd;
        }
    }
    return mainWindowHandle();
}

void bringWindowToDialogFront(HWND hwnd)
{
    if (!hwnd) {
        return;
    }
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(hwnd);
}

std::string openFileDialog(const wchar_t* title, const wchar_t* filter)
{
    return fileDialog(title, filter, false);
}

std::string saveFileDialog(const wchar_t* title, const wchar_t* filter)
{
    return fileDialog(title, filter, true);
}

std::string selectFolderDialog(const wchar_t* title, const std::filesystem::path& initial)
{
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) {
        return {};
    }

    DWORD options = 0;
    if (SUCCEEDED(dialog->GetOptions(&options))) {
        dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    }
    dialog->SetTitle(title);

    IShellItem* initialFolder = nullptr;
    std::error_code ec;
    if (!initial.empty() && std::filesystem::exists(initial, ec) &&
        SUCCEEDED(SHCreateItemFromParsingName(initial.wstring().c_str(), nullptr, IID_PPV_ARGS(&initialFolder)))) {
        dialog->SetFolder(initialFolder);
        initialFolder->Release();
    }

    HWND owner = dialogOwnerHandle();
    bringWindowToDialogFront(owner);
    const HRESULT result = dialog->Show(owner);
    bringWindowToDialogFront(owner);
    if (FAILED(result)) {
        dialog->Release();
        return {};
    }

    IShellItem* selected = nullptr;
    if (FAILED(dialog->GetResult(&selected))) {
        dialog->Release();
        return {};
    }
    PWSTR path = nullptr;
    std::string selectedPath;
    if (SUCCEEDED(selected->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
        selectedPath = narrow(path);
        CoTaskMemFree(path);
    }
    selected->Release();
    dialog->Release();
    return selectedPath;
}

std::string pickIconDialog(const std::string& current)
{
    wchar_t file[MAX_PATH]{};
    std::wstring initial = current.empty() ? L"%SystemRoot%\\System32\\SHELL32.dll" : widen(current);
    wcsncpy_s(file, MAX_PATH, initial.c_str(), _TRUNCATE);
    int iconIndex = 0;
    HWND owner = dialogOwnerHandle();
    using PickIconDlgFn = int(WINAPI*)(HWND, PWSTR, UINT, int*);
    HMODULE shell = GetModuleHandleW(L"shell32.dll");
    auto pickIcon = shell ? reinterpret_cast<PickIconDlgFn>(GetProcAddress(shell, MAKEINTRESOURCEA(62))) : nullptr;
    bringWindowToDialogFront(owner);
    const bool accepted = pickIcon && pickIcon(owner, file, MAX_PATH, &iconIndex);
    bringWindowToDialogFront(owner);
    if (!accepted) {
        return {};
    }
    return narrow(file);
}

void openContainingFolder(const LaunchItem& item)
{
    if (item.target.empty()) {
        return;
    }

    // Prefer absolute native path. UTF-8 path strings from global search are converted via widen().
    std::filesystem::path targetPath = item.target;
    std::error_code ec;
    if (!targetPath.is_absolute()) {
        const std::filesystem::path absolute = std::filesystem::absolute(targetPath, ec);
        if (!ec) {
            targetPath = absolute;
        }
    }
    targetPath = targetPath.lexically_normal();

    const bool exists = std::filesystem::exists(targetPath, ec);
    std::wstring native = targetPath.wstring();
    // explorer /select is picky: use backslashes and quote only the path token.
    for (wchar_t& ch : native) {
        if (ch == L'/') {
            ch = L'\\';
        }
    }

    if (exists) {
        // ILCreateFromPath + SHOpenFolderAndSelectItems is more reliable for non-ASCII paths.
        if (PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(native.c_str())) {
            const HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
            ILFree(pidl);
            if (SUCCEEDED(hr)) {
                return;
            }
        }
        const std::wstring args = L"/select,\"" + native + L"\"";
        if (reinterpret_cast<intptr_t>(ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL)) > 32) {
            return;
        }
    }

    // Fallback: open parent folder (or the path itself if it is a directory).
    std::filesystem::path folder = targetPath;
    if (!std::filesystem::is_directory(folder, ec)) {
        folder = folder.parent_path();
    }
    if (!folder.empty()) {
        ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

void openWithDialog(const LaunchItem& item)
{
    if (item.target.empty()) {
        return;
    }
    const std::wstring target = item.target.wstring();
    HWND owner = dialogOwnerHandle();
    OPENASINFO info{};
    info.pcszFile = target.c_str();
    info.oaifInFlags = OAIF_ALLOW_REGISTRATION | OAIF_EXEC;
    bringWindowToDialogFront(owner);
    if (FAILED(SHOpenWithDialog(owner, &info))) {
        const std::wstring args = L"shell32.dll,OpenAs_RunDLL \"" + target + L"\"";
        ShellExecuteW(nullptr, L"open", L"rundll32.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
    bringWindowToDialogFront(owner);
}

void showFileProperties(const LaunchItem& item)
{
    if (item.target.empty()) {
        return;
    }
    const std::wstring target = item.target.wstring();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_INVOKEIDLIST;
    info.lpVerb = L"properties";
    info.lpFile = target.c_str();
    info.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&info);
}

void openAppFolder()
{
    ShellExecuteW(nullptr, L"open", appFolder().wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void broadcastEnvironmentChanged()
{
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 2000, nullptr);
}

void openUrl(const wchar_t* url)
{
    ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL);
}

void openSystemTool(const wchar_t* command, const wchar_t* args)
{
    ShellExecuteW(nullptr, L"open", command, args, nullptr, SW_SHOWNORMAL);
}

void centerMainWindow()
{
    HWND hwnd = mainWindowHandle();
    if (!hwnd) {
        return;
    }
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        return;
    }
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) {
        return;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int x = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
    const int y = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

} // namespace launcher
