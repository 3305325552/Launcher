#include "app/Application.hpp"

#include "launcher/AppIdentity.hpp"
#include "core/StringEncoding.hpp"
#include "platform/SystemIntegration.hpp"
#include "platform/Win32Window.hpp"
#include "ui/MainDock.hpp"
#include "ui/Localization.hpp"
#include "ui/MaterialIconRegistry.hpp"
#include "ui/UiAnimation.hpp"
#include "ui/UiTheme.hpp"

#include <windows.h>
#include <dwmapi.h>

#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <tlhelp32.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace launcher {
namespace {

HWND gLastLauncherForeground = nullptr;

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
constexpr std::int64_t kUpdateCheckIntervalSeconds = 24 * 60 * 60;

std::filesystem::path firstExistingFont()
{
    wchar_t winDir[MAX_PATH]{};
    if (GetWindowsDirectoryW(winDir, MAX_PATH) == 0) {
        return {};
    }
    const std::filesystem::path fontDir = std::filesystem::path(winDir) / "Fonts";
    for (const char* name : {"msyh.ttc", "simhei.ttf", "simsun.ttc", "segoeui.ttf"}) {
        std::filesystem::path path = fontDir / name;
        if (std::filesystem::exists(path)) {
            return path;
        }
    }
    return {};
}

void loadFonts()
{
    ImGuiIO& io = ImGui::GetIO();
    std::filesystem::path textFont = firstExistingFont();
    if (!textFont.empty()) {
        io.Fonts->AddFontFromFileTTF(textFont.string().c_str(), 18.0f, nullptr, io.Fonts->GetGlyphRangesChineseSimplifiedCommon());
    }

    std::filesystem::path iconFont = getAssetDir() / "fonts" / "MaterialIcons-Regular.ttf";
    if (std::filesystem::exists(iconFont)) {
        ImFontConfig config;
        config.MergeMode = true;
        config.PixelSnapH = true;
        config.GlyphMinAdvanceX = 18.0f;
        config.GlyphOffset.y = 3.5f;
        const ImVector<ImWchar>& iconRanges = materialIconGlyphRanges();
        if (!iconRanges.empty()) {
            io.Fonts->AddFontFromFileTTF(iconFont.string().c_str(), 18.0f, &config, iconRanges.Data);
        }
    }
}

void setProcessEfficiencyMode(bool enabled)
{
    static bool active = false;
    if (enabled == active) {
        return;
    }

    HANDLE process = GetCurrentProcess();
    using SetProcessInformationFn = BOOL(WINAPI*)(HANDLE, PROCESS_INFORMATION_CLASS, LPVOID, DWORD);
    const auto setProcessInformation =
        reinterpret_cast<SetProcessInformationFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessInformation"));
    using SetThreadInformationFn = BOOL(WINAPI*)(HANDLE, int, LPVOID, DWORD);
    const auto setThreadInformation =
        reinterpret_cast<SetThreadInformationFn>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadInformation"));
    constexpr int kThreadPowerThrottling = 3;
    struct ThreadPowerThrottlingState {
        ULONG Version;
        ULONG ControlMask;
        ULONG StateMask;
    };
    constexpr ULONG kThreadPowerThrottlingCurrentVersion = 1;
    constexpr ULONG kThreadPowerThrottlingExecutionSpeed = 1;
    struct MemoryPriorityInfo {
        ULONG MemoryPriority;
    };
    if (enabled) {
        SetPriorityClass(process, PROCESS_MODE_BACKGROUND_BEGIN);

        if (setProcessInformation) {
            PROCESS_POWER_THROTTLING_STATE power{};
            power.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            power.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
            power.StateMask = power.ControlMask;
            setProcessInformation(process, ProcessPowerThrottling, &power, sizeof(power));

            MemoryPriorityInfo memory{};
            memory.MemoryPriority = MEMORY_PRIORITY_LOW;
            setProcessInformation(process, ProcessMemoryPriority, &memory, sizeof(memory));
        }
        if (setThreadInformation) {
            ThreadPowerThrottlingState threadPower{};
            threadPower.Version = kThreadPowerThrottlingCurrentVersion;
            threadPower.ControlMask = kThreadPowerThrottlingExecutionSpeed;
            threadPower.StateMask = kThreadPowerThrottlingExecutionSpeed;
            setThreadInformation(GetCurrentThread(), kThreadPowerThrottling, &threadPower, sizeof(threadPower));
        }
    } else {
        SetPriorityClass(process, PROCESS_MODE_BACKGROUND_END);

        if (setProcessInformation) {
            PROCESS_POWER_THROTTLING_STATE power{};
            power.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
            power.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
            power.StateMask = 0;
            setProcessInformation(process, ProcessPowerThrottling, &power, sizeof(power));

            MemoryPriorityInfo memory{};
            memory.MemoryPriority = MEMORY_PRIORITY_NORMAL;
            setProcessInformation(process, ProcessMemoryPriority, &memory, sizeof(memory));
        }
        if (setThreadInformation) {
            ThreadPowerThrottlingState threadPower{};
            threadPower.Version = kThreadPowerThrottlingCurrentVersion;
            threadPower.ControlMask = kThreadPowerThrottlingExecutionSpeed;
            threadPower.StateMask = 0;
            setThreadInformation(GetCurrentThread(), kThreadPowerThrottling, &threadPower, sizeof(threadPower));
        }
    }
    active = enabled;
}

void trimProcessMemory()
{
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

bool belongsToCurrentProcess(HWND hwnd)
{
    DWORD pid = 0;
    if (hwnd) {
        GetWindowThreadProcessId(hwnd, &pid);
    }
    return pid == GetCurrentProcessId();
}

std::vector<HWND> visibleProcessWindows()
{
    std::vector<HWND> windows;
    EnumWindows(
        [](HWND hwnd, LPARAM userData) -> BOOL {
            if (!IsWindowVisible(hwnd)) {
                return TRUE;
            }
            if (!belongsToCurrentProcess(hwnd)) {
                return TRUE;
            }
            auto* result = reinterpret_cast<std::vector<HWND>*>(userData);
            result->push_back(hwnd);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));
    return windows;
}

bool cursorInsideAnyWindow(const std::vector<HWND>& windows)
{
    POINT pt{};
    GetCursorPos(&pt);
    for (HWND hwnd : windows) {
        RECT rect{};
        if (GetWindowRect(hwnd, &rect) && PtInRect(&rect, pt)) {
            return true;
        }
    }
    return false;
}

std::int64_t nowUnix()
{
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::tm localTime(std::int64_t value)
{
    std::time_t raw = static_cast<std::time_t>(value);
    std::tm out{};
    localtime_s(&out, &raw);
    return out;
}

std::int64_t makeLocalTime(std::tm value)
{
    value.tm_isdst = -1;
    return static_cast<std::int64_t>(std::mktime(&value));
}

std::int64_t nextDailyRun(const ScheduledTask& task, std::int64_t now)
{
    std::tm tm = localTime(now);
    tm.tm_hour = task.hour;
    tm.tm_min = task.minute;
    tm.tm_sec = 0;
    std::int64_t candidate = makeLocalTime(tm);
    if (candidate <= now) {
        tm = localTime(now + 24 * 60 * 60);
        tm.tm_hour = task.hour;
        tm.tm_min = task.minute;
        tm.tm_sec = 0;
        candidate = makeLocalTime(tm);
    }
    return candidate;
}

std::int64_t nextWeeklyRun(const ScheduledTask& task, std::int64_t now)
{
    const int mask = task.weekdayMask == 0 ? 0x7F : task.weekdayMask;
    for (int days = 0; days <= 7; ++days) {
        std::tm tm = localTime(now + static_cast<std::int64_t>(days) * 24 * 60 * 60);
        const int bit = 1 << tm.tm_wday;
        if ((mask & bit) == 0) {
            continue;
        }
        tm.tm_hour = task.hour;
        tm.tm_min = task.minute;
        tm.tm_sec = 0;
        const std::int64_t candidate = makeLocalTime(tm);
        if (candidate > now) {
            return candidate;
        }
    }
    return nextDailyRun(task, now + 7 * 24 * 60 * 60);
}

std::int64_t computeNextRun(const ScheduledTask& task, std::int64_t now)
{
    if (!task.enabled) {
        return 0;
    }
    switch (task.trigger) {
    case ScheduledTriggerKind::Once: return task.onceAt > task.lastRunAt ? task.onceAt : 0;
    case ScheduledTriggerKind::Daily: return nextDailyRun(task, now);
    case ScheduledTriggerKind::Weekly: return nextWeeklyRun(task, now);
    case ScheduledTriggerKind::Interval: {
        const std::int64_t base = task.lastRunAt > 0 ? task.lastRunAt : now;
        const std::int64_t next = base + static_cast<std::int64_t>(std::max(1, task.intervalMinutes)) * 60;
        return next <= now ? now : next;
    }
    case ScheduledTriggerKind::AppStart:
    case ScheduledTriggerKind::WakeUnlock:
    case ScheduledTriggerKind::ProcessStart:
    default: return 0;
    }
}

std::unordered_set<std::string> runningProcessNames()
{
    std::unordered_set<std::string> names;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return names;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            names.insert(lowerAscii(narrow(entry.szExeFile)));
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return names;
}

void applyViewportWindowAttributes(HWND mainHwnd, bool topmostLayerActive)
{
    static std::unordered_map<HWND, bool> appliedTopmost;
    static std::unordered_map<HWND, bool> appliedChrome;

    auto rememberWindow = [](std::vector<HWND>& windows, HWND hwnd) {
        if (hwnd && std::find(windows.begin(), windows.end(), hwnd) == windows.end()) {
            windows.push_back(hwnd);
        }
    };

    std::vector<HWND> windows;
    rememberWindow(windows, mainHwnd);

    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    for (ImGuiViewport* viewport : platform.Viewports) {
        if (auto* hwnd = static_cast<HWND>(viewport->PlatformHandleRaw)) {
            rememberWindow(windows, hwnd);
        }
    }
    EnumWindows(
        [](HWND hwnd, LPARAM userData) -> BOOL {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            if (pid != GetCurrentProcessId()) {
                return TRUE;
            }
            auto* processWindows = reinterpret_cast<std::vector<HWND>*>(userData);
            if (std::find(processWindows->begin(), processWindows->end(), hwnd) == processWindows->end()) {
                processWindows->push_back(hwnd);
            }
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&windows));

    for (auto it = appliedChrome.begin(); it != appliedChrome.end();) {
        if (!IsWindow(it->first) || std::find(windows.begin(), windows.end(), it->first) == windows.end()) {
            it = appliedChrome.erase(it);
        } else {
            ++it;
        }
    }

    for (HWND hwnd : windows) {
        if (appliedChrome.find(hwnd) != appliedChrome.end()) {
            continue;
        }
        const int roundPreference = 2;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &roundPreference, sizeof(roundPreference));
        const COLORREF borderColor = kDwmColorNone;
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
        appliedChrome[hwnd] = true;
    }

    for (auto it = appliedTopmost.begin(); it != appliedTopmost.end();) {
        if (!IsWindow(it->first) || std::find(windows.begin(), windows.end(), it->first) == windows.end()) {
            it = appliedTopmost.erase(it);
        } else {
            ++it;
        }
    }

    HWND foreground = GetForegroundWindow();
    if (belongsToCurrentProcess(foreground)) {
        gLastLauncherForeground = foreground;
    }

    auto syncTopmost = [&](HWND hwnd, bool force) {
        const auto existing = appliedTopmost.find(hwnd);
        if (!force && existing != appliedTopmost.end() && existing->second == topmostLayerActive) {
            return;
        }
        SetWindowPos(hwnd, topmostLayerActive ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
        appliedTopmost[hwnd] = topmostLayerActive;
    };

    for (HWND hwnd : windows) {
        syncTopmost(hwnd, false);
    }
}

} // namespace

Application::Application()
{
    SystemIntegration::registerTaskbarAppId();
    context_.load();
    syncDirectorySearchContextMenu();
    context_.themes.reload(getAssetDir() / "themes", context_.config.directory() / "themes", context_.persisted().settings.themeId);
    context_.persisted().settings.themeId = context_.themes.active().id;
    const AppSettings& settings = context_.persisted().settings;
    window_ = std::make_unique<Win32Window>(app_identity::kDisplayName, 960, 640, settings.startHidden);
    window_->setMinimizeToTray(settings.minimizeToTray);
    window_->setGlobalHotkey(settings.globalHotkey);
    window_->setGlobalHotkeyEnabled(settings.enableGlobalHotkey);
    window_->setSearchHotkey(settings.searchHotkey);
    window_->setSearchHotkeyEnabled(settings.enableSearchHotkey);
    setupImGui();
    window_->setToggleVisibleCallback([this]() {
        toggleWindowGroup();
    });
    window_->setSearchRequestedCallback([this]() {
        showWindowGroup();
        openMainDockSearch(context_);
    });
    window_->setSearchTextRequestedCallback([this](std::wstring text) {
        openSearchWithText(narrow(text));
    });
    window_->setWakeUnlockCallback([this]() {
        handleWakeUnlockTrigger();
    });
    window_->setShowRequestedCallback([this]() {
        showWindowGroup();
    });
    window_->setHideRequestedCallback([this]() {
        hideWindowGroup();
    });
    window_->setTrayTextProvider([](const char* key) {
        return trw(key);
    });
    window_->setDropFilesCallback([this](std::vector<std::filesystem::path> paths) {
        addDroppedFiles(std::move(paths));
    });
    window_->setLiveResizeCallback([this]() {
        frame();
    });
}

Application::~Application()
{
    shutdownImGui();
}

int Application::run()
{
    while (window_->processMessages()) {
        tickScheduledTasks();
        if (window_->isVisible()) {
            setProcessEfficiencyMode(false);
            frame();
        } else {
            setProcessEfficiencyMode(true);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    context_.save();
    return 0;
}

void Application::openSearchWithText(std::string text)
{
    showWindowGroup();
    openMainDockSearchWithText(context_, std::move(text));
}

void Application::syncDirectorySearchContextMenu()
{
    const bool enabled = context_.persisted().settings.directorySearchContextMenu;
    if (directorySearchContextMenuSynced_ && directorySearchContextMenuLastEnabled_ == enabled) {
        return;
    }
    SystemIntegration::setDirectorySearchContextMenuEnabled(enabled);
    directorySearchContextMenuSynced_ = true;
    directorySearchContextMenuLastEnabled_ = enabled;
}

void Application::handleWakeUnlockTrigger()
{
    const std::int64_t now = nowUnix();
    bool changed = false;
    for (ScheduledTask& task : context_.persisted().scheduledTasks) {
        if (!task.enabled || task.trigger != ScheduledTriggerKind::WakeUnlock) {
            continue;
        }
        std::string message;
        const bool ok = context_.runScheduledTaskAction(task, &message);
        task.lastRunAt = now;
        task.lastSuccess = ok;
        task.lastMessage = message;
        task.history.insert(task.history.begin(), ScheduledTaskHistory{now, nowUnix(), ok, message});
        if (task.history.size() > 20) {
            task.history.resize(20);
        }
        changed = true;
    }
    if (changed) {
        context_.save();
    }
}

void Application::tickScheduledTasks()
{
    const double nowTime = ImGui::GetTime();
    if (lastTaskTick_ > 0.0 && nowTime - lastTaskTick_ < 1.0) {
        return;
    }
    lastTaskTick_ = nowTime;

    const std::int64_t now = nowUnix();
    tickAutomaticUpdateCheck();
    bool changed = false;
    if (!appStartTasksHandled_) {
        appStartTasksHandled_ = true;
        for (ScheduledTask& task : context_.persisted().scheduledTasks) {
            if (!task.enabled || task.trigger != ScheduledTriggerKind::AppStart) {
                continue;
            }
            std::string message;
            const bool ok = context_.runScheduledTaskAction(task, &message);
            task.lastRunAt = now;
            task.lastSuccess = ok;
            task.lastMessage = message;
            task.history.insert(task.history.begin(), ScheduledTaskHistory{now, nowUnix(), ok, message});
            if (task.history.size() > 20) task.history.resize(20);
            changed = true;
        }
    }

    const std::unordered_set<std::string> processes = runningProcessNames();
    std::unordered_map<std::string, bool> nextProcessSeen = processTriggerSeen_;
    for (ScheduledTask& task : context_.persisted().scheduledTasks) {
        if (!task.enabled) {
            continue;
        }
        const std::string processName = lowerAscii(task.processName);
        if (task.trigger == ScheduledTriggerKind::ProcessStart && !processName.empty()) {
            const bool running = processes.contains(processName);
            const bool seen = processTriggerSeen_[processName];
            if (running && !seen) {
                std::string message;
                const bool ok = context_.runScheduledTaskAction(task, &message);
                task.lastRunAt = now;
                task.lastSuccess = ok;
                task.lastMessage = message;
                task.history.insert(task.history.begin(), ScheduledTaskHistory{now, nowUnix(), ok, message});
                if (task.history.size() > 20) task.history.resize(20);
                changed = true;
            }
            nextProcessSeen[processName] = running;
            continue;
        }

        if (task.trigger == ScheduledTriggerKind::AppStart || task.trigger == ScheduledTriggerKind::WakeUnlock ||
            task.trigger == ScheduledTriggerKind::ProcessStart) {
            continue;
        }
        if (task.nextRunAt <= 0) {
            task.nextRunAt = computeNextRun(task, now);
            changed = true;
        }
        const bool retryDue = task.retryAt > 0 && task.retryAt <= now && task.pendingRetries > 0;
        const bool due = task.nextRunAt > 0 && task.nextRunAt <= now;
        if (!retryDue && !due) {
            continue;
        }
        if (!task.runMissed && due && now - task.nextRunAt > 5 * 60) {
            task.nextRunAt = computeNextRun(task, now);
            changed = true;
            continue;
        }

        std::string message;
        const std::int64_t startedAt = nowUnix();
        const bool ok = context_.runScheduledTaskAction(task, &message);
        const std::int64_t finishedAt = nowUnix();
        task.lastRunAt = startedAt;
        task.lastSuccess = ok;
        task.lastMessage = message;
        task.history.insert(task.history.begin(), ScheduledTaskHistory{startedAt, finishedAt, ok, message});
        if (task.history.size() > 20) task.history.resize(20);
        if (!ok && task.pendingRetries < task.retryCount) {
            ++task.pendingRetries;
            task.retryAt = finishedAt + task.retryDelaySeconds;
        } else {
            task.pendingRetries = 0;
            task.retryAt = 0;
            if (task.trigger == ScheduledTriggerKind::Once) {
                task.enabled = false;
                task.nextRunAt = 0;
            } else {
                task.nextRunAt = computeNextRun(task, finishedAt);
            }
        }
        changed = true;
    }
    processTriggerSeen_ = std::move(nextProcessSeen);

    if (changed) {
        context_.save();
    }
}

void Application::tickAutomaticUpdateCheck()
{
    AppSettings& settings = context_.persisted().settings;
    if (!settings.autoCheckUpdates) {
        automaticUpdateCheckHandled_ = true;
        return;
    }

    const std::int64_t now = nowUnix();
    const bool startupDue = !automaticUpdateCheckHandled_ && settings.checkUpdatesAtStartup;
    automaticUpdateCheckHandled_ = true;
    const bool dailyDue =
        settings.checkUpdatesDaily && (settings.lastUpdateCheckAt <= 0 || now - settings.lastUpdateCheckAt >= kUpdateCheckIntervalSeconds);
    if ((!startupDue && !dailyDue) || !context_.updates.checkForUpdates(true)) {
        return;
    }

    settings.lastUpdateCheckAt = now;
    context_.save();
}

void Application::setupImGui()
{
    if (imguiReady_) {
        return;
    }
    window_->ensureGraphicsResources();
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags &= ~ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigViewportsNoAutoMerge = false;

    ImGui::StyleColorsDark();
    loadFonts();
    ImGui_ImplWin32_Init(window_->hwnd());
    ImGui_ImplDX11_Init(window_->device(), window_->deviceContext());
    setMainDockDevice(window_->device());
    imguiReady_ = true;
}

void Application::shutdownImGui()
{
    if (!imguiReady_) {
        return;
    }
    setMainDockDevice(nullptr);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    imguiReady_ = false;
}

bool Application::foregroundBelongsToApp() const
{
    return belongsToCurrentProcess(GetForegroundWindow());
}

void Application::processAutoHideRules()
{
    const AppSettings& settings = context_.persisted().settings;
    if (!window_->isVisible()) {
        mouseLeftWindowGroupStartedAt_ = 0.0;
        focusLostStartedAt_ = 0.0;
        return;
    }

    const double now = ImGui::GetTime();
    if (settings.hideOnFocusLost && !foregroundBelongsToApp()) {
        if (focusLostStartedAt_ <= 0.0) {
            focusLostStartedAt_ = now;
        } else if (now - focusLostStartedAt_ > 0.20) {
            hideWindowGroupAfterFrame_ = true;
            focusLostStartedAt_ = 0.0;
            mouseLeftWindowGroupStartedAt_ = 0.0;
            return;
        }
    } else {
        focusLostStartedAt_ = 0.0;
    }

    if (settings.hideOnMouseLeave) {
        const std::vector<HWND> windows = visibleProcessWindows();
        const bool inside = cursorInsideAnyWindow(windows);
        if (!inside) {
            if (mouseLeftWindowGroupStartedAt_ <= 0.0) {
                mouseLeftWindowGroupStartedAt_ = now;
            } else if (now - mouseLeftWindowGroupStartedAt_ > 0.55) {
                hideWindowGroupAfterFrame_ = true;
                mouseLeftWindowGroupStartedAt_ = 0.0;
                focusLostStartedAt_ = 0.0;
            }
        } else {
            mouseLeftWindowGroupStartedAt_ = 0.0;
        }
    } else {
        mouseLeftWindowGroupStartedAt_ = 0.0;
    }
}

void Application::addDroppedFiles(std::vector<std::filesystem::path> paths)
{
    PersistedState& persisted = context_.persisted();
    RuntimeState& runtime = context_.runtime();
    if (paths.empty() || persisted.categories.empty()) {
        return;
    }

    const int categoryIndex = std::clamp(runtime.selectedCategory, 0, static_cast<int>(persisted.categories.size()) - 1);
    Category& category = persisted.categories[categoryIndex];
    std::vector<LaunchItem>* targetItems = &category.items;
    std::vector<std::string> validStack;
    for (const std::string& id : runtime.currentFolderStack) {
        auto it = std::find_if(targetItems->begin(), targetItems->end(), [&](const LaunchItem& item) {
            return item.id == id && item.type == LaunchItemType::VirtualFolder;
        });
        if (it == targetItems->end()) {
            break;
        }
        validStack.push_back(id);
        targetItems = &it->children;
    }
    if (validStack.size() != runtime.currentFolderStack.size()) {
        runtime.currentFolderStack = std::move(validStack);
    }
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const std::int64_t timestamp = static_cast<std::int64_t>(now);

    for (const std::filesystem::path& path : paths) {
        if (path.empty()) {
            continue;
        }
        const std::string fallbackName = path.filename().string();
        std::string name = path.stem().string();
        if (name.empty()) {
            name = fallbackName.empty() ? "New Item" : fallbackName;
        }

        LaunchItem item;
        item.id = "drop-" + std::to_string(targetItems->size() + 1) + "-" + std::to_string(timestamp);
        item.name = name;
        item.subtitle = path.filename().string();
        item.target = path;
        item.startDirectory = path.has_parent_path() ? path.parent_path() : std::filesystem::path{};
        item.type = LaunchItemType::App;
        item.createdAt = timestamp;
        item.lastEditedAt = timestamp;
        targetItems->push_back(std::move(item));
    }

    runtime.selectedCategory = categoryIndex;
    context_.commitContentChange();
}

void Application::hidePlatformWindows()
{
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    const HWND mainHwnd = window_->hwnd();
    for (ImGuiViewport* viewport : platform.Viewports) {
        if (auto* hwnd = static_cast<HWND>(viewport->PlatformHandleRaw)) {
            if (hwnd != mainHwnd) {
                ShowWindow(hwnd, SW_HIDE);
            }
        }
    }
}

void Application::raiseWindowGroup(bool activateMain)
{
    const HWND mainHwnd = window_->hwnd();
    const HWND activateTarget = (gLastLauncherForeground && IsWindow(gLastLauncherForeground) && IsWindowVisible(gLastLauncherForeground))
                                    ? gLastLauncherForeground
                                    : mainHwnd;
    ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
    for (ImGuiViewport* viewport : platform.Viewports) {
        if (auto* hwnd = static_cast<HWND>(viewport->PlatformHandleRaw)) {
            const bool isActivateTarget = hwnd == activateTarget;
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | (activateMain && isActivateTarget ? 0 : SWP_NOACTIVATE) | SWP_NOSENDCHANGING);
        }
    }

    if (activateMain && activateTarget) {
        SetWindowPos(activateTarget, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOSENDCHANGING);
        BringWindowToTop(activateTarget);
        SetForegroundWindow(activateTarget);
        SetFocus(activateTarget);
    }
}

void Application::showWindowGroup()
{
    if (!window_->isVisible() && window_->wakeSuppressed()) {
        return;
    }
    if (context_.persisted().settings.keepScrollAfterWake) {
        resetMainDockScrollOnNextFrame();
    }
    window_->show(true);
    raiseWindowGroup(true);
    setProcessEfficiencyMode(false);
    trimMemoryFramesRemaining_ = 3;
}

void Application::hideWindowGroup()
{
    closeMainDockWindows(context_);
    hidePlatformWindows();
    window_->hideToTray();
    // Refresh the hidden back buffer after clearing transient UI state. Otherwise
    // ShowWindow may briefly present the last visible frame with stale selection.
    frame();
    releaseMainDockBackgroundCache();
    setProcessEfficiencyMode(true);
    trimProcessMemory();
}

void Application::toggleWindowGroup()
{
    if (window_->isVisible()) {
        hideWindowGroup();
        return;
    }
    showWindowGroup();
}

void Application::frame()
{
    if (renderingFrame_ || !imguiReady_) {
        return;
    }
    renderingFrame_ = true;

    const AppSettings& settings = context_.persisted().settings;
    context_.syncGlobalSearch();
    syncDirectorySearchContextMenu();
    tickScheduledTasks();
    window_->setMinimizeToTray(settings.minimizeToTray);
    window_->setAlwaysOnTop(settings.alwaysOnTop);
    window_->setFullscreenDoNotDisturb(settings.fullscreenDoNotDisturb);
    window_->setWakeByTrayHover(settings.wakeByTrayHover);
    window_->setGlobalHotkey(settings.globalHotkey);
    window_->setGlobalHotkeyEnabled(settings.enableGlobalHotkey);
    window_->setSearchHotkey(settings.searchHotkey);
    window_->setSearchHotkeyEnabled(settings.enableSearchHotkey);
    window_->setWindowPositionLocked(settings.lockWindowPosition);
    window_->setWindowSizeLocked(settings.lockWindowSize);
    window_->setTaskbarIconVisible(settings.showTaskbarIcon);
    const ThemeDefinition& theme = context_.themes.active();
    const UiPalette activePalette = uiPalette(theme);
    window_->setWindowOpacity(theme.windowTransparent, theme.windowOpacity);
    window_->setClearColor(ImGui::ColorConvertU32ToFloat4(activePalette.contentBg));
    window_->setCornerRounding(activePalette.windowRounding);
    window_->beginFrame();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 clientPos = window_->clientScreenPos();
    const ImVec2 clientSize = window_->clientSize();
    io.DisplaySize = clientSize;
    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    mainViewport->Pos = clientPos;
    mainViewport->Size = clientSize;
    mainViewport->WorkPos = clientPos;
    mainViewport->WorkSize = clientSize;
    ImGui::NewFrame();
    ui_anim::beginFrame(settings);

    drawMainDock(context_);
    processAutoHideRules();

    ImGui::Render();
    window_->render(ImGui::GetDrawData());

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        applyViewportWindowAttributes(window_->hwnd(), window_->topmostLayerActive());
        ImGui::RenderPlatformWindowsDefault();
    }

    window_->present(true);
    if (trimMemoryFramesRemaining_ > 0) {
        trimProcessMemory();
        --trimMemoryFramesRemaining_;
    }
    renderingFrame_ = false;
    if (hideWindowGroupAfterFrame_) {
        hideWindowGroupAfterFrame_ = false;
        hideWindowGroup();
    }
}

} // namespace launcher
