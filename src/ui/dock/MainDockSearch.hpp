#pragma once

#include "core/SearchIndex.hpp"
#include "launcher/Models.hpp"
#include "ui/common/UiTheme.hpp"

#include <imgui.h>

#include <cstdint>
#include <atomic>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace launcher {

struct AppContext;

struct SearchResultsCache {
    struct PendingTask {
        std::future<std::vector<SearchResult>> results;
        std::shared_ptr<std::atomic_bool> cancelled;
        std::string query;
        std::string settingsKey;
        std::uint64_t indexRevision = 0;
        std::uint64_t generation = 0;
    };

    std::vector<SearchResult> results;
    std::string query;
    std::string settingsKey;
    std::uint64_t indexRevision = 0;
    std::uint64_t generation = 0;
    std::uint64_t nextGeneration = 1;
    int resultPage = 0;
    int resultPageJump = 1;
    bool selectionFromMouse = false;
    bool valid = false;
    std::vector<PendingTask> pendingTasks;
};

struct SearchUiState {
    bool* focusSearch = nullptr;
    bool* searchOpen = nullptr;
    bool* searchSubmit = nullptr;
    int* searchSelected = nullptr;
    int* searchMove = nullptr;
    int* searchPageMove = nullptr;
    std::string* searchQueryText = nullptr;
    double* searchEditedAt = nullptr;
    bool* searchCursorEndRequested = nullptr;
    SearchResultsCache* searchResultsCache = nullptr;
};

struct SearchUiApi {
    LaunchItem* (*findItemById)(AppContext&, const std::string&) = nullptr;
    void (*runItem)(AppContext&, LaunchItem&, int) = nullptr;
    void (*drawLaunchIcon)(const LaunchItem&, const ImVec2&, float) = nullptr;
    bool (*drawCachedLaunchIcon)(const LaunchItem&, const ImVec2&, float) = nullptr;
    void (*requestLaunchIcon)(const LaunchItem&) = nullptr;
    void (*openGlobalFileEditor)(AppContext&, const LaunchItem&) = nullptr;
    void (*openNote)(AppContext&, const std::string&) = nullptr;
    void (*copyItemToClipboard)(const LaunchItem&, bool) = nullptr;
    void (*pasteClipboardItem)(AppContext&) = nullptr;
    bool (*clipboardAvailable)() = nullptr;
    void (*copyTextToClipboard)(const std::string&) = nullptr;
    std::string (*itemPropertiesText)(const LaunchItem&) = nullptr;
    void (*openWithDialog)(const LaunchItem&) = nullptr;
    void (*openContainingFolder)(const LaunchItem&) = nullptr;
    void (*showFileProperties)(const LaunchItem&) = nullptr;
};

void drawSearchBar(const UiPalette& theme, AppContext& context, SearchUiState state, const ImVec2& origin, float width, float searchHeight);
void drawSearchResults(const UiPalette& theme, AppContext& context, SearchUiState state, const SearchUiApi& api, const ImVec2& origin,
                       const ImVec2& size);

} // namespace launcher
