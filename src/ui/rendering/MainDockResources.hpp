#pragma once

#include <d3d11.h>
#include <memory>

struct ImDrawList;
struct ImVec2;

namespace launcher {

struct AppContext;
struct LaunchItem;
struct ThemeDefinition;

class MainDockResources {
public:
    MainDockResources();
    ~MainDockResources();

    MainDockResources(const MainDockResources&) = delete;
    MainDockResources& operator=(const MainDockResources&) = delete;

    void setDevice(ID3D11Device* device);
    void clear();
    void clearIcons(bool trimWorkingSet = false);
    void clearBackground();

    void resetIconLoadScheduling();
    void beginIconLoadFrame(const AppContext& context, bool searchOpen, bool useDefaultIcons,
                            const char* searchQueryText);
    void processPendingIconRequests(bool useDefaultIcons);
    void requestLaunchIcon(const LaunchItem& item, bool useDefaultIcons);
    void clearIconForItem(const LaunchItem& item);

    bool drawLaunchIcon(ImDrawList* drawList, const LaunchItem& item, const ImVec2& pos, float size,
                        bool useDefaultIcons);
    bool drawCachedLaunchIcon(const LaunchItem& item, const ImVec2& pos, float size,
                              bool useDefaultIcons);

    bool hasBackground(const ThemeDefinition& theme) const;
    void drawBackground(const AppContext& context, const ThemeDefinition& theme, const ImVec2& origin,
                        const ImVec2& size);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace launcher
