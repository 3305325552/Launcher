#pragma once

#include "app/AppContext.hpp"
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace launcher {

class Win32Window;

class Application {
public:
    Application();
    ~Application();

    int run();
    void openSearchWithText(std::string text);

private:
    void setupImGui();
    void shutdownImGui();
    void frame();
    void showWindowGroup();
    void hideWindowGroup();
    void toggleWindowGroup();
    void hidePlatformWindows();
    void raiseWindowGroup(bool activateMain);
    void syncDirectorySearchContextMenu();
    void tickScheduledTasks();
    void tickAutomaticUpdateCheck();
    void handleWakeUnlockTrigger();
    void processAutoHideRules();
    bool foregroundBelongsToApp() const;
    void addDroppedFiles(std::vector<std::filesystem::path> paths);

    AppContext context_;
    std::unique_ptr<Win32Window> window_;
    bool renderingFrame_ = false;
    bool imguiReady_ = false;
    int trimMemoryFramesRemaining_ = 3;
    bool directorySearchContextMenuSynced_ = false;
    bool directorySearchContextMenuLastEnabled_ = false;
    bool hideWindowGroupAfterFrame_ = false;
    double mouseLeftWindowGroupStartedAt_ = 0.0;
    double focusLostStartedAt_ = 0.0;
    double lastTaskTick_ = 0.0;
    bool appStartTasksHandled_ = false;
    bool automaticUpdateCheckHandled_ = false;
    std::unordered_map<std::string, bool> processTriggerSeen_;
};

} // namespace launcher
