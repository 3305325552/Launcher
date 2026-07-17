#pragma once

#include <windows.h>
#include <d3d11.h>
#include <imgui.h>
#include <wrl/client.h>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace launcher {

class Win32Window {
public:
    Win32Window(const std::string& title, int width, int height, bool startHidden = false);
    ~Win32Window();

    bool processMessages();
    void ensureGraphicsResources();
    void releaseGraphicsResources();
    bool hasGraphicsResources() const
    {
        return device_ && deviceContext_ && swapChain_ && renderTarget_;
    }
    void beginFrame();
    void render(ImDrawData* drawData);
    void present(bool vsync = true);
    void show(bool forceTopmost = false);
    void hideToTray();
    bool isVisible() const;
    void setMinimizeToTray(bool enabled)
    {
        minimizeToTray_ = enabled;
    }
    void setAlwaysOnTop(bool enabled);
    void setFullscreenDoNotDisturb(bool enabled)
    {
        fullscreenDoNotDisturb_ = enabled;
    }
    void setWakeByTrayHover(bool enabled)
    {
        wakeByTrayHover_ = enabled;
    }
    void setGlobalHotkeyEnabled(bool enabled);
    void setGlobalHotkey(const std::string& hotkey);
    void setSearchHotkeyEnabled(bool enabled);
    void setSearchHotkey(const std::string& hotkey);
    void setTaskbarIconVisible(bool visible);
    void setWindowPositionLocked(bool locked)
    {
        windowPositionLocked_ = locked;
    }
    void setWindowSizeLocked(bool locked)
    {
        windowSizeLocked_ = locked;
    }
    void setLiveResizeCallback(std::function<void()> callback);
    void setToggleVisibleCallback(std::function<void()> callback);
    void setSearchRequestedCallback(std::function<void()> callback);
    void setSearchTextRequestedCallback(std::function<void(std::wstring)> callback);
    void setShowRequestedCallback(std::function<void()> callback);
    void setHideRequestedCallback(std::function<void()> callback);
    void setWakeUnlockCallback(std::function<void()> callback);
    void setDropFilesCallback(std::function<void(std::vector<std::filesystem::path>)> callback);
    void setTrayTextProvider(std::function<std::wstring(const char*)> provider);
    void setWindowOpacity(bool enabled, int opacity);
    void setClearColor(const ImVec4& color);
    void setCornerRounding(float rounding);
    bool wakeSuppressed() const;

    HWND hwnd() const
    {
        return hwnd_;
    }
    bool topmostLayerActive() const
    {
        return alwaysOnTop_ || transientTopmost_;
    }
    ID3D11Device* device() const
    {
        return device_.Get();
    }
    ID3D11DeviceContext* deviceContext() const
    {
        return deviceContext_.Get();
    }
    ImVec2 clientSize() const;
    ImVec2 clientScreenPos() const;

private:
    static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK keyboardProc(int code, WPARAM wParam, LPARAM lParam);

    void createDevice();
    void createRenderTarget();
    void cleanupRenderTarget();
    void resize(UINT width, UINT height);
    void applyWindowRegion();
    void addTrayIcon();
    void removeTrayIcon();
    void showTrayMenu();
    void toggleVisibleByHotkey();
    void showSearchByHotkey();
    void demoteTransientTopmost();
    void liveResize();
    void installKeyboardHook();
    void removeKeyboardHook();
    struct HotkeyBinding {
        int id = 0;
        UINT modifiers = MOD_NOREPEAT;
        UINT key = 0;
        std::string text;
        bool enabled = false;
        bool nativeRegistered = false;
        bool down = false;
    };
    void setHotkeyEnabled(HotkeyBinding& binding, bool enabled);
    void setHotkey(HotkeyBinding& binding, const std::string& hotkey);
    void syncKeyboardHook();
    HotkeyBinding* hotkeyForId(WPARAM id);
    HotkeyBinding* fallbackHotkeyForKey(UINT vk);
    bool matchesHotkey(const HotkeyBinding& binding, UINT vk) const;
    bool isForegroundFullscreen() const;

    static Win32Window* hotkeyWindow_;

    HWND hwnd_ = nullptr;
    bool minimizeToTray_ = true;
    bool alwaysOnTop_ = false;
    bool transientTopmost_ = false;
    bool fullscreenDoNotDisturb_ = true;
    bool wakeByTrayHover_ = true;
    HotkeyBinding globalHotkey_;
    HotkeyBinding searchHotkey_;
    HHOOK keyboardHook_ = nullptr;
    bool taskbarIconVisible_ = false;
    bool opacityEnabled_ = false;
    int opacity_ = 255;
    int cornerRoundingPx_ = 0;
    float clearColor_[4] = {0.98f, 0.98f, 0.98f, 1.0f};
    bool windowPositionLocked_ = false;
    bool windowSizeLocked_ = false;
    bool allowExit_ = false;
    bool inLiveResize_ = false;
    std::function<void()> liveResizeCallback_;
    std::function<void()> toggleVisibleCallback_;
    std::function<void()> searchRequestedCallback_;
    std::function<void(std::wstring)> searchTextRequestedCallback_;
    std::function<void()> showRequestedCallback_;
    std::function<void()> hideRequestedCallback_;
    std::function<void()> wakeUnlockCallback_;
    std::function<void(std::vector<std::filesystem::path>)> dropFilesCallback_;
    std::function<std::wstring(const char*)> trayTextProvider_;
    HICON appIcon_ = nullptr;
    UINT backBufferWidth_ = 0;
    UINT backBufferHeight_ = 0;
    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> deviceContext_;
    Microsoft::WRL::ComPtr<IDXGISwapChain> swapChain_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> renderTarget_;
};

} // namespace launcher
