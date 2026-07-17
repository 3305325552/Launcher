#include "platform/Win32Window.hpp"

#include "launcher/AppIdentity.hpp"
#include "platform/SystemIntegration.hpp"

#include <dwmapi.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <windows.h>
#include <wtsapi32.h>

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <windowsx.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace launcher {

Win32Window* Win32Window::hotkeyWindow_ = nullptr;

namespace {

constexpr int kResizeBorder = 8;
constexpr UINT kTrayMessage = WM_APP + 25;
constexpr UINT_PTR kTrayId = 1001;
constexpr UINT kTrayShow = 3001;
constexpr UINT kTrayExit = 3002;
constexpr int kToggleHotkeyId = 2501;
constexpr int kSearchHotkeyId = 2502;
constexpr const wchar_t* kWindowClassName = app_identity::kWindowClassName;
const UINT kTaskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
#endif
#ifndef DWMWA_BORDER_COLOR
constexpr DWORD DWMWA_BORDER_COLOR = 34;
#endif
#ifndef DWMWA_COLOR_NONE
constexpr COLORREF kDwmColorNone = 0xFFFFFFFE;
#else
constexpr COLORREF kDwmColorNone = DWMWA_COLOR_NONE;
#endif

void suppressNativeBorder(HWND hwnd)
{
    if (!hwnd) {
        return;
    }
    const int roundPreference = 2;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &roundPreference, sizeof(roundPreference));
    const COLORREF borderColor = kDwmColorNone;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
}

std::string normalizedToken(std::string value)
{
    value.erase(std::remove_if(value.begin(), value.end(),
                               [](unsigned char ch) {
                                   return std::isspace(ch) != 0 || ch == '.';
                               }),
                value.end());
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    if (value.starts_with("VK")) {
        value.erase(0, 2);
    }
    return value;
}

std::vector<std::string> splitHotkey(const std::string& text)
{
    std::vector<std::string> tokens;
    std::string current;
    for (char ch : text) {
        if (ch == '+' || ch == ',') {
            if (!current.empty()) {
                tokens.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        tokens.push_back(current);
    }
    return tokens;
}

enum class PreferredAppMode {
    Default,
    AllowDark,
    ForceDark,
    ForceLight,
    Max,
};

void useNativeSystemMenuTheme()
{
    HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll");
    if (!uxtheme) {
        uxtheme = LoadLibraryW(L"uxtheme.dll");
    }
    if (!uxtheme) {
        return;
    }

    using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
    using FlushMenuThemesFn = void(WINAPI*)();
    auto setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(135)));
    auto flushMenuThemes = reinterpret_cast<FlushMenuThemesFn>(GetProcAddress(uxtheme, MAKEINTRESOURCEA(136)));
    if (setPreferredAppMode) {
        setPreferredAppMode(PreferredAppMode::AllowDark);
    }
    if (flushMenuThemes) {
        flushMenuThemes();
    }
}

UINT keyFromToken(const std::string& token)
{
    if (token == "SPACE") return VK_SPACE;
    if (token == "ESC" || token == "ESCAPE") return VK_ESCAPE;
    if (token == "TAB") return VK_TAB;
    if (token == "ENTER" || token == "RETURN") return VK_RETURN;
    if (token == "BACKSPACE") return VK_BACK;
    if (token == "DELETE" || token == "DEL") return VK_DELETE;
    if (token == "INSERT" || token == "INS") return VK_INSERT;
    if (token == "HOME") return VK_HOME;
    if (token == "END") return VK_END;
    if (token == "PAGEUP" || token == "PRIOR") return VK_PRIOR;
    if (token == "PAGEDOWN" || token == "NEXT") return VK_NEXT;
    if (token == "UP") return VK_UP;
    if (token == "DOWN") return VK_DOWN;
    if (token == "LEFT") return VK_LEFT;
    if (token == "RIGHT") return VK_RIGHT;
    if (token.size() == 1 && token[0] >= 'A' && token[0] <= 'Z') return static_cast<UINT>(token[0]);
    if (token.size() == 1 && token[0] >= '0' && token[0] <= '9') return static_cast<UINT>(token[0]);
    if (token.size() >= 2 && token[0] == 'F') {
        int fn = std::atoi(token.c_str() + 1);
        if (fn >= 1 && fn <= 24) return VK_F1 + static_cast<UINT>(fn - 1);
    }
    return 0;
}

bool parseHotkey(const std::string& text, UINT& modifiers, UINT& key)
{
    modifiers = MOD_NOREPEAT;
    key = 0;
    for (std::string token : splitHotkey(text)) {
        token = normalizedToken(token);
        if (token == "CTRL" || token == "CONTROL" || token == "LCONTROL" || token == "RCONTROL" || token == "LCTRL" || token == "RCTRL") {
            modifiers |= MOD_CONTROL;
        } else if (token == "ALT" || token == "MENU" || token == "LALT" || token == "RALT") {
            modifiers |= MOD_ALT;
        } else if (token == "SHIFT" || token == "LSHIFT" || token == "RSHIFT") {
            modifiers |= MOD_SHIFT;
        } else if (token == "WIN" || token == "LWIN" || token == "RWIN" || token == "META") {
            modifiers |= MOD_WIN;
        } else {
            key = keyFromToken(token);
        }
    }
    return key != 0;
}

std::wstring lowerWide(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring windowClassName(HWND hwnd)
{
    wchar_t buffer[128]{};
    GetClassNameW(hwnd, buffer, static_cast<int>(sizeof(buffer) / sizeof(buffer[0])));
    return buffer;
}

std::wstring processName(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return {};
    }

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return {};
    }

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::wstring result;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == pid) {
                result = entry.szExeFile;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return lowerWide(result);
}

bool containsAny(const std::wstring& value, std::initializer_list<const wchar_t*> needles)
{
    for (const wchar_t* needle : needles) {
        if (value.find(needle) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

bool isShellLikeWindow(HWND hwnd)
{
    if (hwnd == GetDesktopWindow() || hwnd == GetShellWindow()) {
        return true;
    }

    const std::wstring cls = windowClassName(hwnd);
    return cls == L"Progman" || cls == L"WorkerW" || cls == L"Shell_TrayWnd";
}

bool isKnownProductivityWindow(HWND hwnd)
{
    const std::wstring exe = processName(hwnd);
    return containsAny(exe, {L"code.exe", L"cursor.exe", L"devenv.exe", L"clion", L"idea", L"rider", L"qtcreator", L"windsurf", L"trae",
                             L"zed", L"zed-editor"});
}

bool isKnownGameWindow(HWND hwnd)
{
    const std::wstring cls = lowerWide(windowClassName(hwnd));
    return containsAny(cls, {L"unrealwindow", L"unitywndclass", L"sdl_app", L"glfw", L"cryengine", L"gamewindow"});
}

HICON loadAppIcon()
{
    const std::filesystem::path iconPath = getAssetDir() / "icon" / "icon.ico";
    if (!std::filesystem::exists(iconPath)) {
        return nullptr;
    }
    return static_cast<HICON>(LoadImageW(nullptr, iconPath.wstring().c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
}

POINT centeredWindowPosition(int width, int height)
{
    RECT workArea{};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0)) {
        workArea = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }

    const int workWidth = workArea.right - workArea.left;
    const int workHeight = workArea.bottom - workArea.top;
    POINT result{};
    result.x = workArea.left + std::max(0, (workWidth - width) / 2);
    result.y = workArea.top + std::max(0, (workHeight - height) / 2);
    return result;
}

LRESULT hitTestBorderlessWindow(HWND hwnd, LPARAM lParam, bool /*lockPosition*/, bool lockSize)
{
    RECT rect{};
    GetWindowRect(hwnd, &rect);

    const int x = GET_X_LPARAM(lParam);
    const int y = GET_Y_LPARAM(lParam);
    if (!lockSize) {
        const bool left = x >= rect.left && x < rect.left + kResizeBorder;
        const bool right = x < rect.right && x >= rect.right - kResizeBorder;
        const bool top = y >= rect.top && y < rect.top + kResizeBorder;
        const bool bottom = y < rect.bottom && y >= rect.bottom - kResizeBorder;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
    }

    return HTCLIENT;
}

} // namespace

Win32Window::Win32Window(const std::string& title, int width, int height, bool startHidden)
{
    globalHotkey_ = {kToggleHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_SPACE, "Ctrl+Alt+Space", false, false, false};
    searchHotkey_ = {kSearchHotkeyId, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'F', "Ctrl+Alt+F", false, false, false};
    appIcon_ = loadAppIcon();
    HICON fallbackIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    WNDCLASSEXW wc{.cbSize = sizeof(WNDCLASSEXW),
                   .style = CS_CLASSDC,
                   .lpfnWndProc = Win32Window::wndProc,
                   .hInstance = GetModuleHandleW(nullptr),
                   .hIcon = appIcon_ ? appIcon_ : fallbackIcon,
                   .hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512)),
                   .lpszClassName = kWindowClassName,
                   .hIconSm = appIcon_ ? appIcon_ : fallbackIcon};
    RegisterClassExW(&wc);

    const std::wstring wideTitle = widen(title);
    const POINT initialPos = centeredWindowPosition(width, height);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, wc.lpszClassName, wideTitle.c_str(),
                            WS_POPUP | WS_MINIMIZEBOX | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, initialPos.x, initialPos.y, width, height,
                            nullptr, nullptr, wc.hInstance, this);

    if (!hwnd_) {
        throw std::runtime_error("CreateWindowW failed");
    }
    DragAcceptFiles(hwnd_, TRUE);

    suppressNativeBorder(hwnd_);

    createDevice();
    addTrayIcon();
    WTSRegisterSessionNotification(hwnd_, NOTIFY_FOR_THIS_SESSION);
    if (startHidden) {
        ShowWindow(hwnd_, SW_HIDE);
    } else {
        show(true);
    }
    UpdateWindow(hwnd_);
}

Win32Window::~Win32Window()
{
    cleanupRenderTarget();
    swapChain_.Reset();
    deviceContext_.Reset();
    device_.Reset();

    removeTrayIcon();
    if (hwnd_) {
        WTSUnRegisterSessionNotification(hwnd_);
    }
    setSearchHotkeyEnabled(false);
    setGlobalHotkeyEnabled(false);
    if (hwnd_) {
        DestroyWindow(hwnd_);
    }
    if (appIcon_) {
        DestroyIcon(appIcon_);
        appIcon_ = nullptr;
    }
    UnregisterClassW(kWindowClassName, GetModuleHandleW(nullptr));
}

bool Win32Window::processMessages()
{
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
        if (msg.message == WM_HOTKEY) {
            if (HotkeyBinding* binding = hotkeyForId(msg.wParam)) {
                if (binding->id == kSearchHotkeyId) {
                    showSearchByHotkey();
                } else {
                    toggleVisibleByHotkey();
                }
                continue;
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT) {
            return false;
        }
    }
    return true;
}

void Win32Window::ensureGraphicsResources()
{
    if (!device_ || !deviceContext_ || !swapChain_) {
        createDevice();
        return;
    }
    if (!renderTarget_) {
        createRenderTarget();
    }
}

void Win32Window::releaseGraphicsResources()
{
    if (deviceContext_) {
        deviceContext_->OMSetRenderTargets(0, nullptr, nullptr);
        deviceContext_->ClearState();
        deviceContext_->Flush();
    }
    cleanupRenderTarget();
    swapChain_.Reset();
    deviceContext_.Reset();
    device_.Reset();
    backBufferWidth_ = 0;
    backBufferHeight_ = 0;
}

void Win32Window::beginFrame()
{
    ensureGraphicsResources();
    RECT client{};
    GetClientRect(hwnd_, &client);
    const UINT width = static_cast<UINT>(client.right - client.left);
    const UINT height = static_cast<UINT>(client.bottom - client.top);
    if (width > 0 && height > 0 && (width != backBufferWidth_ || height != backBufferHeight_)) {
        resize(width, height);
    }

    deviceContext_->OMSetRenderTargets(1, renderTarget_.GetAddressOf(), nullptr);
    deviceContext_->ClearRenderTargetView(renderTarget_.Get(), clearColor_);
}

void Win32Window::render(ImDrawData* drawData)
{
    if (deviceContext_) {
        ImGui_ImplDX11_RenderDrawData(drawData);
    }
}

void Win32Window::present(bool vsync)
{
    if (swapChain_) {
        swapChain_->Present(vsync ? 1 : 0, 0);
    }
}

void Win32Window::show(bool forceTopmost)
{
    ShowWindow(hwnd_, IsIconic(hwnd_) ? SW_RESTORE : SW_SHOWNORMAL);
    const bool shouldTopmost = alwaysOnTop_ || forceTopmost;
    if (forceTopmost && !alwaysOnTop_) {
        transientTopmost_ = true;
    }
    SetWindowPos(hwnd_, shouldTopmost ? HWND_TOPMOST : HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    HWND foreground = GetForegroundWindow();
    const DWORD currentThread = GetCurrentThreadId();
    const DWORD foregroundThread = foreground ? GetWindowThreadProcessId(foreground, nullptr) : 0;
    if (foregroundThread != 0 && foregroundThread != currentThread) {
        AttachThreadInput(currentThread, foregroundThread, TRUE);
    }
    BringWindowToTop(hwnd_);
    SetForegroundWindow(hwnd_);
    SetActiveWindow(hwnd_);
    SetFocus(hwnd_);
    if (foregroundThread != 0 && foregroundThread != currentThread) {
        AttachThreadInput(currentThread, foregroundThread, FALSE);
    }
}

void Win32Window::hideToTray()
{
    demoteTransientTopmost();
    ShowWindow(hwnd_, SW_HIDE);
}

bool Win32Window::isVisible() const
{
    return IsWindowVisible(hwnd_) != FALSE;
}

void Win32Window::setAlwaysOnTop(bool enabled)
{
    if (enabled == alwaysOnTop_) {
        return;
    }
    alwaysOnTop_ = enabled;
    if (!enabled) {
        transientTopmost_ = false;
    }
    SetWindowPos(hwnd_, enabled ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void Win32Window::setGlobalHotkeyEnabled(bool enabled)
{
    setHotkeyEnabled(globalHotkey_, enabled);
}

void Win32Window::setGlobalHotkey(const std::string& hotkey)
{
    setHotkey(globalHotkey_, hotkey);
}

void Win32Window::setSearchHotkeyEnabled(bool enabled)
{
    setHotkeyEnabled(searchHotkey_, enabled);
}

void Win32Window::setSearchHotkey(const std::string& hotkey)
{
    setHotkey(searchHotkey_, hotkey);
}

void Win32Window::setHotkeyEnabled(HotkeyBinding& binding, bool enabled)
{
    if (enabled == binding.enabled) {
        return;
    }

    if (!enabled) {
        if (binding.nativeRegistered) {
            UnregisterHotKey(hwnd_, binding.id);
            binding.nativeRegistered = false;
        }
        binding.enabled = false;
        binding.down = false;
        syncKeyboardHook();
        return;
    }

    binding.nativeRegistered = RegisterHotKey(hwnd_, binding.id, binding.modifiers, binding.key) != FALSE;
    binding.enabled = true;
    binding.down = false;
    syncKeyboardHook();
}

void Win32Window::setHotkey(HotkeyBinding& binding, const std::string& hotkey)
{
    if (hotkey == binding.text) {
        return;
    }

    UINT modifiers = 0;
    UINT key = 0;
    if (!parseHotkey(hotkey, modifiers, key)) {
        return;
    }

    const bool wasEnabled = binding.enabled;
    if (wasEnabled) {
        setHotkeyEnabled(binding, false);
    }
    binding.text = hotkey;
    binding.modifiers = modifiers;
    binding.key = key;
    if (wasEnabled) {
        setHotkeyEnabled(binding, true);
    }
}

void Win32Window::setTaskbarIconVisible(bool visible)
{
    if (visible == taskbarIconVisible_) {
        return;
    }

    const bool wasVisible = IsWindowVisible(hwnd_) != FALSE;
    HWND foreground = GetForegroundWindow();
    HWND restoreAbove = (foreground && foreground != hwnd_) ? foreground : nullptr;
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (visible) {
        exStyle &= ~WS_EX_TOOLWINDOW;
        exStyle |= WS_EX_APPWINDOW;
    } else {
        exStyle &= ~WS_EX_APPWINDOW;
        exStyle |= WS_EX_TOOLWINDOW;
    }
    taskbarIconVisible_ = visible;
    if (wasVisible) {
        ShowWindow(hwnd_, SW_HIDE);
    }
    SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
    SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_NOSENDCHANGING);
    if (wasVisible) {
        ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
        if (restoreAbove && IsWindow(restoreAbove) && IsWindowVisible(restoreAbove)) {
            SetWindowPos(hwnd_, restoreAbove, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
            SetForegroundWindow(restoreAbove);
        }
    }
}

void Win32Window::setWindowOpacity(bool enabled, int opacity)
{
    opacity = std::clamp(opacity, 1, 100);
    if (enabled == opacityEnabled_ && opacity == opacity_) {
        return;
    }

    opacityEnabled_ = enabled;
    opacity_ = opacity;
    const BYTE alpha = static_cast<BYTE>(std::clamp(opacity * 255 / 100, 1, 255));
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    if (enabled && opacity < 100) {
        exStyle |= WS_EX_LAYERED;
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
        SetLayeredWindowAttributes(hwnd_, 0, alpha, LWA_ALPHA);
    } else {
        exStyle &= ~WS_EX_LAYERED;
        SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, exStyle);
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
    }
}

void Win32Window::setClearColor(const ImVec4& color)
{
    clearColor_[0] = std::clamp(color.x, 0.0f, 1.0f);
    clearColor_[1] = std::clamp(color.y, 0.0f, 1.0f);
    clearColor_[2] = std::clamp(color.z, 0.0f, 1.0f);
    clearColor_[3] = 1.0f;
}

void Win32Window::setCornerRounding(float rounding)
{
    const int radius = std::clamp(static_cast<int>(std::lround(rounding)), 0, 96);
    if (radius == cornerRoundingPx_) {
        return;
    }
    cornerRoundingPx_ = radius;
    applyWindowRegion();
}

void Win32Window::applyWindowRegion()
{
    if (!hwnd_) {
        return;
    }
    suppressNativeBorder(hwnd_);
    if (cornerRoundingPx_ <= 0) {
        SetWindowRgn(hwnd_, nullptr, TRUE);
        return;
    }

    RECT rect{};
    if (!GetWindowRect(hwnd_, &rect)) {
        return;
    }
    const int width = std::max(1L, rect.right - rect.left);
    const int height = std::max(1L, rect.bottom - rect.top);
    const int diameter = std::max(1, cornerRoundingPx_ * 2);
    HRGN region = CreateRoundRectRgn(0, 0, width + 1, height + 1, diameter, diameter);
    if (!region) {
        return;
    }
    if (SetWindowRgn(hwnd_, region, TRUE) == 0) {
        DeleteObject(region);
    }
}
void Win32Window::setLiveResizeCallback(std::function<void()> callback)
{
    liveResizeCallback_ = std::move(callback);
}

void Win32Window::setToggleVisibleCallback(std::function<void()> callback)
{
    toggleVisibleCallback_ = std::move(callback);
}

void Win32Window::setSearchRequestedCallback(std::function<void()> callback)
{
    searchRequestedCallback_ = std::move(callback);
}

void Win32Window::setSearchTextRequestedCallback(std::function<void(std::wstring)> callback)
{
    searchTextRequestedCallback_ = std::move(callback);
}

void Win32Window::setShowRequestedCallback(std::function<void()> callback)
{
    showRequestedCallback_ = std::move(callback);
}

void Win32Window::setWakeUnlockCallback(std::function<void()> callback)
{
    wakeUnlockCallback_ = std::move(callback);
}

void Win32Window::setHideRequestedCallback(std::function<void()> callback)
{
    hideRequestedCallback_ = std::move(callback);
}

void Win32Window::setDropFilesCallback(std::function<void(std::vector<std::filesystem::path>)> callback)
{
    dropFilesCallback_ = std::move(callback);
}

void Win32Window::setTrayTextProvider(std::function<std::wstring(const char*)> provider)
{
    trayTextProvider_ = std::move(provider);
}

ImVec2 Win32Window::clientSize() const
{
    RECT client{};
    GetClientRect(hwnd_, &client);
    return ImVec2(static_cast<float>(client.right - client.left), static_cast<float>(client.bottom - client.top));
}

ImVec2 Win32Window::clientScreenPos() const
{
    POINT origin{0, 0};
    ClientToScreen(hwnd_, &origin);
    return ImVec2(static_cast<float>(origin.x), static_cast<float>(origin.y));
}

void Win32Window::createDevice()
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd_;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    D3D_FEATURE_LEVEL featureLevel{};
    constexpr D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};

    HRESULT hr =
        D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, featureLevels, 2, D3D11_SDK_VERSION, &sd,
                                      swapChain_.GetAddressOf(), device_.GetAddressOf(), &featureLevel, deviceContext_.GetAddressOf());

    if (FAILED(hr)) {
        throw std::runtime_error("D3D11CreateDeviceAndSwapChain failed");
    }

    createRenderTarget();
}

void Win32Window::createRenderTarget()
{
    Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
    swapChain_->GetBuffer(0, IID_PPV_ARGS(backBuffer.GetAddressOf()));
    device_->CreateRenderTargetView(backBuffer.Get(), nullptr, renderTarget_.GetAddressOf());
}

void Win32Window::cleanupRenderTarget()
{
    renderTarget_.Reset();
}

void Win32Window::resize(UINT width, UINT height)
{
    if (!device_ || width == 0 || height == 0) {
        return;
    }

    deviceContext_->OMSetRenderTargets(0, nullptr, nullptr);
    cleanupRenderTarget();
    deviceContext_->Flush();
    if (SUCCEEDED(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))) {
        backBufferWidth_ = width;
        backBufferHeight_ = height;
        createRenderTarget();
        applyWindowRegion();
        InvalidateRect(hwnd_, nullptr, TRUE);
    }
}

void Win32Window::liveResize()
{
    if (inLiveResize_ || !liveResizeCallback_) {
        return;
    }

    inLiveResize_ = true;
    liveResizeCallback_();
    inLiveResize_ = false;
}

void Win32Window::installKeyboardHook()
{
    if (keyboardHook_) {
        return;
    }

    hotkeyWindow_ = this;
    globalHotkey_.down = false;
    searchHotkey_.down = false;
    keyboardHook_ = SetWindowsHookExW(WH_KEYBOARD_LL, Win32Window::keyboardProc, GetModuleHandleW(nullptr), 0);
    if (!keyboardHook_ && hotkeyWindow_ == this) {
        hotkeyWindow_ = nullptr;
    }
}

void Win32Window::removeKeyboardHook()
{
    if (keyboardHook_) {
        UnhookWindowsHookEx(keyboardHook_);
        keyboardHook_ = nullptr;
    }
    if (hotkeyWindow_ == this) {
        hotkeyWindow_ = nullptr;
    }
    globalHotkey_.down = false;
    searchHotkey_.down = false;
}

void Win32Window::syncKeyboardHook()
{
    const bool needsHook =
        (globalHotkey_.enabled && !globalHotkey_.nativeRegistered) || (searchHotkey_.enabled && !searchHotkey_.nativeRegistered);
    if (needsHook) {
        installKeyboardHook();
    } else {
        removeKeyboardHook();
    }
}

Win32Window::HotkeyBinding* Win32Window::hotkeyForId(WPARAM id)
{
    if (id == static_cast<WPARAM>(globalHotkey_.id)) {
        return &globalHotkey_;
    }
    if (id == static_cast<WPARAM>(searchHotkey_.id)) {
        return &searchHotkey_;
    }
    return nullptr;
}

Win32Window::HotkeyBinding* Win32Window::fallbackHotkeyForKey(UINT vk)
{
    HotkeyBinding* bindings[] = {&searchHotkey_, &globalHotkey_};
    for (HotkeyBinding* binding : bindings) {
        if (binding->enabled && !binding->nativeRegistered && matchesHotkey(*binding, vk)) {
            return binding;
        }
    }
    return nullptr;
}

bool Win32Window::matchesHotkey(const HotkeyBinding& binding, UINT vk) const
{
    if (vk != binding.key) {
        return false;
    }

    auto down = [](int key) {
        return (GetAsyncKeyState(key) & 0x8000) != 0;
    };
    const bool ctrl = down(VK_CONTROL) || down(VK_LCONTROL) || down(VK_RCONTROL);
    const bool alt = down(VK_MENU) || down(VK_LMENU) || down(VK_RMENU);
    const bool shift = down(VK_SHIFT) || down(VK_LSHIFT) || down(VK_RSHIFT);
    const bool win = down(VK_LWIN) || down(VK_RWIN);

    return ctrl == ((binding.modifiers & MOD_CONTROL) != 0) && alt == ((binding.modifiers & MOD_ALT) != 0) &&
           shift == ((binding.modifiers & MOD_SHIFT) != 0) && win == ((binding.modifiers & MOD_WIN) != 0);
}

void Win32Window::addTrayIcon()
{
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = kTrayMessage;
    nid.hIcon = appIcon_ ? appIcon_ : LoadIconW(nullptr, MAKEINTRESOURCEW(32512));
    wcscpy_s(nid.szTip, std::size(nid.szTip), L"Launcher");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

void Win32Window::removeTrayIcon()
{
    if (!hwnd_) {
        return;
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd_;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void Win32Window::showTrayMenu()
{
    useNativeSystemMenuTheme();

    POINT pt{};
    GetCursorPos(&pt);
    HMENU menu = CreatePopupMenu();
    const std::wstring showText = trayTextProvider_ ? trayTextProvider_("Show Launcher") : L"Show Launcher";
    const std::wstring exitText = trayTextProvider_ ? trayTextProvider_("Exit") : L"Exit";
    AppendMenuW(menu, MF_STRING, kTrayShow, showText.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kTrayExit, exitText.c_str());
    SetForegroundWindow(hwnd_);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
}

void Win32Window::demoteTransientTopmost()
{
    if (!transientTopmost_ || alwaysOnTop_) {
        return;
    }
    transientTopmost_ = false;
    SetWindowPos(hwnd_, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool Win32Window::isForegroundFullscreen() const
{
    HWND foreground = GetForegroundWindow();
    if (!foreground || foreground == hwnd_ || isShellLikeWindow(foreground) || isKnownProductivityWindow(foreground)) {
        return false;
    }

    RECT win{};
    if (!GetWindowRect(foreground, &win)) {
        return false;
    }
    HMONITOR monitor = MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) {
        return false;
    }

    constexpr int tolerance = 2;
    const bool coversMonitor = win.left <= info.rcMonitor.left + tolerance && win.top <= info.rcMonitor.top + tolerance &&
                               win.right >= info.rcMonitor.right - tolerance && win.bottom >= info.rcMonitor.bottom - tolerance;
    if (!coversMonitor) {
        return false;
    }

    const LONG_PTR style = GetWindowLongPtrW(foreground, GWL_STYLE);
    const LONG_PTR exStyle = GetWindowLongPtrW(foreground, GWL_EXSTYLE);
    const bool standardAppWindow = (style & WS_CAPTION) != 0 || (style & WS_THICKFRAME) != 0 || IsZoomed(foreground) != FALSE;
    if (standardAppWindow && !isKnownGameWindow(foreground)) {
        return false;
    }

    const bool topMost = (exStyle & WS_EX_TOPMOST) != 0;
    return topMost || isKnownGameWindow(foreground);
}

bool Win32Window::wakeSuppressed() const
{
    return fullscreenDoNotDisturb_ && isForegroundFullscreen();
}

void Win32Window::toggleVisibleByHotkey()
{
    if (toggleVisibleCallback_) {
        toggleVisibleCallback_();
        return;
    }

    if (IsWindowVisible(hwnd_)) {
        if (GetForegroundWindow() == hwnd_) {
            hideToTray();
        } else {
            show(true);
        }
        return;
    }

    if (fullscreenDoNotDisturb_ && isForegroundFullscreen()) {
        return;
    }
    show(true);
}

void Win32Window::showSearchByHotkey()
{
    if (fullscreenDoNotDisturb_ && !IsWindowVisible(hwnd_) && isForegroundFullscreen()) {
        return;
    }
    if (searchRequestedCallback_) {
        searchRequestedCallback_();
        return;
    }
    show(true);
}

LRESULT CALLBACK Win32Window::wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) {
        return true;
    }

    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    auto* window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == kTaskbarCreatedMsg) {
        if (window) {
            window->addTrayIcon();
        }
        return 0;
    }
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wParam) {
            return 0;
        }
        break;
    case WM_NCHITTEST:
        return hitTestBorderlessWindow(hwnd, lParam, window ? window->windowPositionLocked_ : false,
                                       window ? window->windowSizeLocked_ : false);
    case WM_NCLBUTTONDOWN:
        if (wParam == HTCAPTION) {
            ImGui::ClosePopupsExceptModals();
        }
        break;
    case WM_ENTERSIZEMOVE: ImGui::ClosePopupsExceptModals(); break;
    case WM_SIZE:
        if (window && wParam != SIZE_MINIMIZED) {
            window->resize(static_cast<UINT>(LOWORD(lParam)), static_cast<UINT>(HIWORD(lParam)));
            window->liveResize();
        }
        return 0;
    case WM_SIZING:
        if (window) {
            RECT client{};
            GetClientRect(hwnd, &client);
            const UINT width = static_cast<UINT>(client.right - client.left);
            const UINT height = static_cast<UINT>(client.bottom - client.top);
            if (width > 0 && height > 0) {
                window->resize(width, height);
                window->liveResize();
            }
        }
        return TRUE;
    case WM_SYSCOMMAND:
        if (window) {
            const WPARAM command = wParam & 0xFFF0;
            if (command == SC_MINIMIZE) {
                if (window->hideRequestedCallback_) {
                    window->hideRequestedCallback_();
                } else {
                    window->hideToTray();
                }
                return 0;
            }
            if (command == SC_RESTORE) {
                if (window->showRequestedCallback_) {
                    window->showRequestedCallback_();
                } else {
                    window->show(true);
                }
                return 0;
            }
        }
        break;
    case WM_WINDOWPOSCHANGED:
        if (window) {
            auto* pos = reinterpret_cast<WINDOWPOS*>(lParam);
            if (pos && (pos->flags & SWP_NOSIZE)) {
                break;
            }

            RECT client{};
            GetClientRect(hwnd, &client);
            const UINT width = static_cast<UINT>(client.right - client.left);
            const UINT height = static_cast<UINT>(client.bottom - client.top);
            if (width > 0 && height > 0 && (width != window->backBufferWidth_ || height != window->backBufferHeight_)) {
                window->resize(width, height);
                window->liveResize();
            }
        }
        break;
    case WM_ERASEBKGND: return 1;
    case WM_DROPFILES:
        if (window && window->dropFilesCallback_) {
            HDROP drop = reinterpret_cast<HDROP>(wParam);
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            std::vector<std::filesystem::path> paths;
            paths.reserve(count);
            for (UINT i = 0; i < count; ++i) {
                const UINT length = DragQueryFileW(drop, i, nullptr, 0);
                std::wstring path(length + 1, L'\0');
                DragQueryFileW(drop, i, path.data(), length + 1);
                path.resize(length);
                paths.emplace_back(std::move(path));
            }
            DragFinish(drop);
            if (!paths.empty()) {
                window->dropFilesCallback_(std::move(paths));
            }
        }
        return 0;
    case WM_COPYDATA:
        if (window && window->searchTextRequestedCallback_) {
            const auto* copyData = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
            if (copyData && copyData->dwData == app_identity::kSearchTextCopyDataId && copyData->lpData &&
                copyData->cbData >= sizeof(wchar_t)) {
                const size_t length = copyData->cbData / sizeof(wchar_t);
                std::wstring text(static_cast<const wchar_t*>(copyData->lpData), length);
                if (!text.empty() && text.back() == L'\0') {
                    text.pop_back();
                }
                window->searchTextRequestedCallback_(std::move(text));
            }
        }
        return 0;
    case WM_GETMINMAXINFO:
        if (auto* info = reinterpret_cast<MINMAXINFO*>(lParam)) {
            info->ptMinTrackSize.x = 720;
            info->ptMinTrackSize.y = 520;
        }
        return 0;
    case WM_HOTKEY:
        if (window) {
            if (HotkeyBinding* binding = window->hotkeyForId(wParam)) {
                if (binding->id == kSearchHotkeyId) {
                    window->showSearchByHotkey();
                } else {
                    window->toggleVisibleByHotkey();
                }
            }
        }
        return 0;
    case WM_WTSSESSION_CHANGE:
        if (window && (wParam == WTS_SESSION_UNLOCK || wParam == WTS_SESSION_LOGON) && window->wakeUnlockCallback_) {
            window->wakeUnlockCallback_();
        }
        return 0;
    case kTrayMessage:
        if (window && (LOWORD(lParam) == WM_LBUTTONDBLCLK || LOWORD(lParam) == WM_LBUTTONUP)) {
            if (window->showRequestedCallback_) {
                window->showRequestedCallback_();
            } else {
                window->show(true);
            }
        } else if (window && LOWORD(lParam) == WM_MOUSEMOVE && window->wakeByTrayHover_) {
            if (window->showRequestedCallback_) {
                window->showRequestedCallback_();
            } else {
                window->show(true);
            }
        } else if (window && LOWORD(lParam) == WM_RBUTTONUP) {
            window->showTrayMenu();
        }
        return 0;
    case WM_COMMAND:
        if (window && LOWORD(wParam) == kTrayShow) {
            if (window->showRequestedCallback_) {
                window->showRequestedCallback_();
            } else {
                window->show(true);
            }
            return 0;
        }
        if (window && LOWORD(wParam) == kTrayExit) {
            window->allowExit_ = true;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    case WM_CLOSE:
        if (window && window->minimizeToTray_ && !window->allowExit_) {
            if (window->hideRequestedCallback_) {
                window->hideRequestedCallback_();
            } else {
                window->hideToTray();
            }
            return 0;
        }
        break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(hwnd, msg, wParam, lParam);
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK Win32Window::keyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    Win32Window* window = hotkeyWindow_;
    if (code == HC_ACTION && window) {
        const auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
        const bool keyDown = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
        const bool keyUp = wParam == WM_KEYUP || wParam == WM_SYSKEYUP;

        if (keyDown) {
            if (Win32Window::HotkeyBinding* binding = window->fallbackHotkeyForKey(info->vkCode)) {
                if (!binding->down) {
                    binding->down = true;
                    PostMessageW(window->hwnd_, WM_HOTKEY, binding->id, 0);
                }
                return 1;
            }
        }
        if (keyUp) {
            const bool modifierUp = info->vkCode == VK_CONTROL || info->vkCode == VK_LCONTROL || info->vkCode == VK_RCONTROL ||
                                    info->vkCode == VK_MENU || info->vkCode == VK_LMENU || info->vkCode == VK_RMENU ||
                                    info->vkCode == VK_SHIFT || info->vkCode == VK_LSHIFT || info->vkCode == VK_RSHIFT ||
                                    info->vkCode == VK_LWIN || info->vkCode == VK_RWIN;
            if (info->vkCode == window->globalHotkey_.key || modifierUp) {
                window->globalHotkey_.down = false;
            }
            if (info->vkCode == window->searchHotkey_.key || modifierUp) {
                window->searchHotkey_.down = false;
            }
        }
    }
    return CallNextHookEx(window ? window->keyboardHook_ : nullptr, code, wParam, lParam);
}

} // namespace launcher
