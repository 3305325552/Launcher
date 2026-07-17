#pragma once

#include "core/ConfigStore.hpp"
#include "core/GlobalFileIndex.hpp"
#include "core/LauncherService.hpp"
#include "core/NotesStore.hpp"
#include "core/PluginManager.hpp"
#include "core/SearchIndex.hpp"
#include "core/ThemeStore.hpp"
#include "update/UpdateService.hpp"
#include "launcher/Models.hpp"

#include <filesystem>
#include <vector>

namespace launcher {

struct AppContext {
    ~AppContext();

    AppState state;
    ConfigStore config;
    LauncherService launcher;
    NotesStore notes;
    PluginManager plugins;
    SearchIndex search;
    ThemeStore themes;
    GlobalFileIndex globalFiles;
    UpdateService updates;

    PersistedState& persisted()
    {
        return state.persisted();
    }

    const PersistedState& persisted() const
    {
        return state.persisted();
    }

    RuntimeState& runtime()
    {
        return state.runtime();
    }

    const RuntimeState& runtime() const
    {
        return state.runtime();
    }

    void load();
    void save();
    std::vector<std::filesystem::path> pluginRoots() const;
    void reloadPlugins();
    void rebuildSearch();
    void commitContentChange();
    LaunchItem* findItemById(const std::string& id);
    const LaunchItem* findItemById(const std::string& id) const;
    bool runScheduledTaskAction(const ScheduledTask& task, std::string* message = nullptr);
    bool runPluginAction(const PluginSearchResult& result, const std::string& actionId, std::string* message = nullptr);
    void syncGlobalSearch();
    void rebuildGlobalSearch();
    void cancelGlobalSearchRebuild();
    void stopGlobalSearch();
};

} // namespace launcher
