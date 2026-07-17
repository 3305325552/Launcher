#include "app/Application.hpp"
#include "launcher/AppIdentity.hpp"
#include "core/StringEncoding.hpp"

#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>

namespace {

std::wstring quoteSearchDirectory(std::wstring path)
{
    if (path.empty()) {
        return {};
    }
    if (path.starts_with(L"\"") && path.ends_with(L"\"")) {
        return path;
    }
    return L"\"" + path + L"\"";
}

std::wstring requestedSearchText()
{
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        return {};
    }

    std::wstring result;
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if ((arg == L"--search-dir" || arg == L"/search-dir") && i + 1 < argc) {
            result = L"dir " + quoteSearchDirectory(argv[++i]) + L" ";
            break;
        }
    }
    LocalFree(argv);
    return result;
}

void sendSearchText(HWND hwnd, const std::wstring& text)
{
    if (!hwnd || text.empty()) {
        return;
    }
    COPYDATASTRUCT copyData{};
    copyData.dwData = launcher::app_identity::kSearchTextCopyDataId;
    copyData.cbData = static_cast<DWORD>((text.size() + 1) * sizeof(wchar_t));
    copyData.lpData = const_cast<wchar_t*>(text.c_str());
    SendMessageW(hwnd, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&copyData));
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    const HRESULT comInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const std::wstring searchText = requestedSearchText();
    HANDLE mutex = CreateMutexW(nullptr, TRUE, launcher::app_identity::kSingleInstanceMutexName);
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (HWND hwnd = FindWindowW(launcher::app_identity::kWindowClassName, nullptr)) {
            sendSearchText(hwnd, searchText);
            ShowWindow(hwnd, SW_SHOWNORMAL);
            SetForegroundWindow(hwnd);
        }
        CloseHandle(mutex);
        if (SUCCEEDED(comInit)) {
            CoUninitialize();
        }
        return 0;
    }

    launcher::Application app;
    if (!searchText.empty()) {
        app.openSearchWithText(launcher::narrow(searchText));
    }
    int result = app.run();
    if (mutex) {
        CloseHandle(mutex);
    }
    if (SUCCEEDED(comInit)) {
        CoUninitialize();
    }
    return result;
}
