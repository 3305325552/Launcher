#include "ui/MainDockSearch.hpp"

#include "app/AppContext.hpp"
#include "core/GlobalFileItem.hpp"
#include "ui/MainDock.hpp"
#include "ui/MainDockChrome.hpp"
#include "ui/MainDockMenu.hpp"
#include "ui/Localization.hpp"
#include "ui/MainDockState.hpp"
#include "ui/MaterialIconRegistry.hpp"
#include "ui/MaterialIcons.hpp"
#include "ui/UiAnimation.hpp"

#include <windows.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <future>
#include <string>
#include <vector>

namespace launcher {
namespace {

constexpr float kSearchResultRowHeight = 56.0f;
constexpr float kSearchFooterHeight = 44.0f;
bool gSearchScrollTopRequested = false;
bool gSearchScrollBottomRequested = false;

ImU32 searchRowHighlight(const UiPalette& theme, bool selected, float amount)
{
    ImVec4 color = selected ? theme.headerActive : theme.headerHovered;
    color.w *= std::clamp(amount, 0.0f, 1.0f);
    return ImGui::ColorConvertFloat4ToU32(color);
}

std::string searchSettingsKey(const AppSettings& settings)
{
    std::string key;
    key.reserve(96);
    auto addBool = [&](bool value) {
        key.push_back(value ? '1' : '0');
        key.push_back('|');
    };
    addBool(settings.advancedSearch);
    addBool(settings.enableGlobalSearch);
    addBool(settings.searchScopeTarget);
    addBool(settings.searchScopeRemark);
    addBool(settings.searchPinyinInitial);
    addBool(settings.searchPinyin);
    addBool(settings.searchEnglishMode);
    addBool(settings.searchRegex);
    addBool(settings.globalSearchHideSystemPaths);
    key += std::to_string(settings.searchDelayMs);
    key.push_back('|');
    for (const PluginPreference& plugin : settings.plugins) {
        key += plugin.id;
        key.push_back('=');
        key.push_back(plugin.enabled ? '1' : '0');
        for (const auto& [settingKey, value] : plugin.settings) {
            key.push_back(',');
            key += settingKey;
            key.push_back(':');
            key += value;
        }
        key.push_back('|');
    }
    return key;
}

std::string scheduledTasksSearchKey(const std::vector<ScheduledTask>& tasks)
{
    std::string key = "tasks:";
    key += std::to_string(tasks.size());
    key.push_back('|');
    for (const ScheduledTask& task : tasks) {
        key += task.id;
        key.push_back(':');
        key += task.name;
        key.push_back(':');
        key.push_back(task.enabled ? '1' : '0');
        key.push_back(':');
        key += task.itemId;
        key.push_back(':');
        key += std::to_string(static_cast<int>(task.trigger));
        key.push_back(':');
        key += std::to_string(static_cast<int>(task.action));
        key.push_back(':');
        key += std::to_string(task.hour);
        key.push_back(':');
        key += std::to_string(task.minute);
        key.push_back(':');
        key += std::to_string(task.weekdayMask);
        key.push_back('|');
    }
    return key;
}

std::string searchCacheKey(const AppSettings& settings, const std::vector<ScheduledTask>& tasks)
{
    return searchSettingsKey(settings) + scheduledTasksSearchKey(tasks);
}

bool searchTaskMatches(const SearchResultsCache::PendingTask& task, const std::string& queryText, const std::string& settingsKey,
                       std::uint64_t revision)
{
    return task.query == queryText && task.settingsKey == settingsKey && task.indexRevision == revision;
}

bool pollSearchResultsCache(SearchResultsCache& cache)
{
    bool hasPending = false;
    for (auto it = cache.pendingTasks.begin(); it != cache.pendingTasks.end();) {
        SearchResultsCache::PendingTask& task = *it;
        if (!task.results.valid()) {
            it = cache.pendingTasks.erase(it);
            continue;
        }
        if (task.results.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            hasPending = true;
            ++it;
            continue;
        }

        std::vector<SearchResult> results = task.results.get();
        const bool publish = !task.cancelled->load(std::memory_order_relaxed) && task.generation >= cache.generation;
        if (publish) {
            cache.results = std::move(results);
            cache.query = task.query;
            cache.settingsKey = task.settingsKey;
            cache.indexRevision = task.indexRevision;
            cache.generation = task.generation;
            cache.resultPage = 0;
            cache.resultPageJump = 1;
            cache.selectionFromMouse = false;
            cache.valid = true;
        }
        it = cache.pendingTasks.erase(it);
    }
    return hasPending;
}

bool hasPendingSearchFor(const SearchResultsCache& cache, const std::string& queryText, const std::string& settingsKey,
                         std::uint64_t revision)
{
    return std::any_of(cache.pendingTasks.begin(), cache.pendingTasks.end(), [&](const SearchResultsCache::PendingTask& task) {
        return !task.cancelled->load(std::memory_order_relaxed) && searchTaskMatches(task, queryText, settingsKey, revision);
    });
}

void cancelOtherSearchTasks(SearchResultsCache& cache, const std::string& queryText, const std::string& settingsKey, std::uint64_t revision)
{
    for (SearchResultsCache::PendingTask& task : cache.pendingTasks) {
        if (!searchTaskMatches(task, queryText, settingsKey, revision)) {
            task.cancelled->store(true, std::memory_order_relaxed);
        }
    }
}

const std::vector<SearchResult>& cachedSearchResults(AppContext& context, SearchUiState state, const std::string& queryText,
                                                     const AppSettings& settings)
{
    static SearchResultsCache fallbackCache;
    SearchResultsCache& cache = state.searchResultsCache ? *state.searchResultsCache : fallbackCache;
    const std::uint64_t revision = context.search.revision();
    const std::string settingsKey = searchCacheKey(settings, context.persisted().scheduledTasks);

    const bool hasPending = pollSearchResultsCache(cache);
    const bool currentReady =
        cache.valid && cache.query == queryText && cache.settingsKey == settingsKey && cache.indexRevision == revision;
    if (!currentReady && cache.valid) {
        cache.results.clear();
        cache.results.shrink_to_fit();
        cache.query.clear();
        cache.settingsKey.clear();
        cache.indexRevision = 0;
        cache.valid = false;
    }
    const bool pendingMatches = hasPending && hasPendingSearchFor(cache, queryText, settingsKey, revision);
    if (!currentReady && !pendingMatches) {
        cancelOtherSearchTasks(cache, queryText, settingsKey, revision);
        AppSettings settingsCopy = settings;
        std::vector<ScheduledTask> scheduledTasks = context.persisted().scheduledTasks;
        SearchIndex* search = &context.search;
        auto cancelled = std::make_shared<std::atomic_bool>(false);
        SearchResultsCache::PendingTask task;
        task.cancelled = cancelled;
        task.query = queryText;
        task.settingsKey = settingsKey;
        task.indexRevision = revision;
        task.generation = cache.nextGeneration++;
        PluginManager* plugins = &context.plugins;
        task.results = std::async(std::launch::async, [search, plugins, query = queryText, settingsCopy = std::move(settingsCopy),
                                                       scheduledTasks = std::move(scheduledTasks), cancelled]() {
            std::vector<SearchResult> results = search->query(query, settingsCopy, cancelled.get());
            if (cancelled && cancelled->load()) {
                return results;
            }
            std::vector<PluginSearchResult> pluginResults =
                plugins ? plugins->query(settingsCopy, scheduledTasks, query, std::max(8, settingsCopy.searchResultLimit / 4))
                        : std::vector<PluginSearchResult>{};
            for (PluginSearchResult& pluginResult : pluginResults) {
                auto owned = std::make_shared<PluginSearchResult>(std::move(pluginResult));
                SearchResult result;
                result.pluginResult = owned;
                result.score = owned->score;
                results.push_back(std::move(result));
            }
            std::sort(results.begin(), results.end(), [](const SearchResult& lhs, const SearchResult& rhs) {
                if (lhs.score != rhs.score) return lhs.score > rhs.score;
                const std::string left = lhs.pluginResult ? lhs.pluginResult->title
                                         : lhs.item       ? lhs.item->name
                                         : lhs.note       ? NotesStore::displayTitle(*lhs.note)
                                                          : std::string{};
                const std::string right = rhs.pluginResult ? rhs.pluginResult->title
                                          : rhs.item       ? rhs.item->name
                                          : rhs.note       ? NotesStore::displayTitle(*rhs.note)
                                                           : std::string{};
                return left < right;
            });
            return results;
        });
        cache.pendingTasks.push_back(std::move(task));
    }
    return cache.results;
}

bool searchResultsPending(SearchUiState state)
{
    const SearchResultsCache* cache = state.searchResultsCache;
    return cache && !cache->pendingTasks.empty();
}

bool searchResultsReadyFor(SearchUiState state, const std::string& queryText, const AppSettings& settings,
                           const std::vector<ScheduledTask>& tasks, std::uint64_t revision)
{
    const SearchResultsCache* cache = state.searchResultsCache;
    if (!cache || !cache->valid) {
        return false;
    }
    return cache->query == queryText && cache->settingsKey == searchCacheKey(settings, tasks) && cache->indexRevision == revision;
}

void drawGenericSearchIcon(ImDrawList* dl, const UiPalette& theme, const ImVec2& pos, float size, bool directory)
{
    const float rounding = 5.0f;
    const ImU32 bg = directory ? IM_COL32(91, 141, 239, 255) : IM_COL32(126, 126, 126, 255);
    dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), bg, rounding);
    const char* glyph = directory ? Icons::Folder : Icons::CopyProperties;
    const float fontSize = std::max(14.0f, size * 0.66f);
    const ImVec2 glyphSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, glyph);
    dl->AddText(nullptr, fontSize, ImVec2(pos.x + (size - glyphSize.x) * 0.5f, pos.y + (size - glyphSize.y) * 0.5f), theme.text, glyph);
}

void drawPluginSearchIcon(ImDrawList* dl, const UiPalette& theme, const ImVec2& pos, float size, const std::string& icon)
{
    const float rounding = 5.0f;
    dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), IM_COL32(96, 116, 152, 255), rounding);
    const char* glyph = materialIconGlyph(icon);
    if (!glyph || glyph[0] == '\0') {
        glyph = Icons::Plugin;
    }
    const float fontSize = std::max(14.0f, size * 0.66f);
    const ImVec2 glyphSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, glyph);
    dl->AddText(nullptr, fontSize, ImVec2(pos.x + (size - glyphSize.x) * 0.5f, pos.y + (size - glyphSize.y) * 0.5f), theme.text, glyph);
}

std::string pluginDisplayName(const AppContext& context, const std::string& pluginId)
{
    for (const PluginInfo& plugin : context.plugins.plugins()) {
        if (plugin.id == pluginId) {
            return plugin.name.empty() ? plugin.id : plugin.name;
        }
    }
    return pluginId;
}

void addClippedText(ImDrawList* dl, const ImVec2& pos, ImU32 color, const std::string& text, const ImVec4& clipRect)
{
    if (text.empty()) {
        return;
    }
    dl->AddText(nullptr, 0.0f, pos, color, text.c_str(), nullptr, 0.0f, &clipRect);
}

void drawSearchLoadingSpinner(ImDrawList* dl, const UiPalette& theme, const ImVec2& center, float alpha)
{
    constexpr int dotCount = 8;
    const float time = static_cast<float>(ImGui::GetTime());
    const float radius = 15.0f;
    for (int i = 0; i < dotCount; ++i) {
        const float angle = time * 4.2f + (static_cast<float>(i) / dotCount) * 6.2831853f;
        const float phase = (std::sin(time * 4.2f - static_cast<float>(i) * 0.72f) + 1.0f) * 0.5f;
        ImVec4 color = ImGui::ColorConvertU32ToFloat4(theme.text);
        color.w *= alpha * (0.25f + phase * 0.75f);
        dl->AddCircleFilled(ImVec2(center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius), 2.2f,
                            ImGui::ColorConvertFloat4ToU32(color), 12);
    }
}

void drawCenteredSearchMessage(const UiPalette& theme, const ImVec2& origin, const ImVec2& size, const char* text, bool loading)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImGuiID id = ImGui::GetID(loading ? "search-loading-message" : "search-empty-message");
    const float alpha = ui_anim::appearAmount(id, 0.18f);
    ImVec4 textColor = ImGui::ColorConvertU32ToFloat4(theme.textMuted);
    textColor.w *= alpha;
    const ImVec2 textSize = ImGui::CalcTextSize(text);
    const ImVec2 center(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
    const float spinnerOffset = loading ? 28.0f : 0.0f;
    if (loading) {
        drawSearchLoadingSpinner(dl, theme, ImVec2(center.x, center.y - 22.0f), alpha);
    }
    dl->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y + spinnerOffset - textSize.y * 0.5f),
                ImGui::ColorConvertFloat4ToU32(textColor), text);
}

int resultPageSize(const AppSettings& settings)
{
    return std::clamp(settings.searchResultLimit, 20, 512);
}

int globalFilesPerPage(const AppSettings& settings)
{
    return std::clamp(settings.globalSearchResultLimit, 0, 512);
}

std::vector<std::vector<size_t>> paginateResults(const std::vector<SearchResult>& results, const AppSettings& settings)
{
    const int pageSize = resultPageSize(settings);
    const int globalLimit = std::min(globalFilesPerPage(settings), pageSize);
    std::vector<std::vector<size_t>> pages;
    pages.emplace_back();
    int globalCount = 0;

    for (size_t index = 0; index < results.size(); ++index) {
        const SearchResult& result = results[index];
        if (result.globalFile && globalLimit == 0) {
            continue;
        }
        if (static_cast<int>(pages.back().size()) >= pageSize || (result.globalFile && globalCount >= globalLimit)) {
            pages.emplace_back();
            globalCount = 0;
        }
        pages.back().push_back(index);
        if (result.globalFile) {
            ++globalCount;
        }
    }
    if (!pages.empty() && pages.back().empty()) {
        pages.pop_back();
    }
    return pages;
}

void setSearchResultPage(int& resultPage, int& pageJumpInput, int page, int pageCount, int* selected)
{
    resultPage = std::clamp(page, 0, std::max(0, pageCount - 1));
    pageJumpInput = resultPage + 1;
    if (selected) {
        *selected = 0;
    }
}

bool drawSearchPaginationControls(const char* id, int& resultPage, int& pageJumpInput, int pageCount, int* selected)
{
    if (pageCount <= 1) {
        return false;
    }

    bool changed = false;
    const float frameHeight = ImGui::GetFrameHeight();
    const ImVec2 arrowButtonSize(frameHeight, frameHeight);
    ImGui::PushID(id);
    ImGui::BeginDisabled(resultPage == 0);
    if (ImGui::Button("<", arrowButtonSize)) {
        setSearchResultPage(resultPage, pageJumpInput, resultPage - 1, pageCount, selected);
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%d / %d", resultPage + 1, pageCount);
    ImGui::SameLine();
    ImGui::BeginDisabled(resultPage + 1 >= pageCount);
    if (ImGui::Button(">", arrowButtonSize)) {
        setSearchResultPage(resultPage, pageJumpInput, resultPage + 1, pageCount, selected);
        changed = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(tr("Page"));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.0f);
    const bool submitted = ImGui::InputInt("##page", &pageJumpInput, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue);
    pageJumpInput = std::clamp(pageJumpInput, 1, pageCount);
    ImGui::SameLine();
    if (ImGui::Button(tr("Go"), ImVec2(0.0f, frameHeight)) || submitted) {
        setSearchResultPage(resultPage, pageJumpInput, pageJumpInput - 1, pageCount, selected);
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

float searchButtonWidth(const char* label)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    return std::max(ImGui::GetFrameHeight(), ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f);
}

float searchPaginationControlsWidth(int page, int pageCount)
{
    if (pageCount <= 1) {
        return 0.0f;
    }

    const ImGuiStyle& style = ImGui::GetStyle();
    const float frameHeight = ImGui::GetFrameHeight();
    char pageText[32]{};
    std::snprintf(pageText, sizeof(pageText), "%d / %d", page + 1, pageCount);

    float width = 0.0f;
    width += frameHeight;
    width += style.ItemSpacing.x + ImGui::CalcTextSize(pageText).x;
    width += style.ItemSpacing.x + frameHeight;
    width += style.ItemSpacing.x + ImGui::CalcTextSize(tr("Page")).x;
    width += style.ItemSpacing.x + 72.0f;
    width += style.ItemSpacing.x + searchButtonWidth(tr("Go"));
    return width;
}

void setCenteredSearchControlsX(float controlsWidth)
{
    const ImVec2 contentMin = ImGui::GetWindowContentRegionMin();
    const ImVec2 contentMax = ImGui::GetWindowContentRegionMax();
    const float contentWidth = std::max(0.0f, contentMax.x - contentMin.x);
    const float x = contentMin.x + std::max(0.0f, (contentWidth - controlsWidth) * 0.5f);
    ImGui::SetCursorPosX(x);
}

void setSearchFooterCursor(const ImVec2& origin, const ImVec2& size, float controlsWidth)
{
    const float x = origin.x + std::max(0.0f, (size.x - controlsWidth) * 0.5f);
    ImGui::SetCursorScreenPos(ImVec2(x, origin.y + size.y - kSearchFooterHeight + 6.0f));
}

float searchBottomControlsWidth(bool showPages, bool showJumpButton, int page, int pageCount)
{
    const ImGuiStyle& style = ImGui::GetStyle();
    float width = showPages ? searchPaginationControlsWidth(page, pageCount) : 0.0f;
    if (showJumpButton) {
        if (width > 0.0f) {
            width += style.ItemSpacing.x;
        }
        width += searchButtonWidth(tr("Top"));
        width += style.ItemSpacing.x + searchButtonWidth(tr("Bottom"));
    }
    return width;
}

ImGuiKey searchShortcutKey(int shortcutIndex)
{
    return shortcutIndex == 9 ? ImGuiKey_0 : static_cast<ImGuiKey>(ImGuiKey_1 + shortcutIndex);
}

std::string searchShortcutText(int shortcutIndex)
{
    const char digit = shortcutIndex == 9 ? '0' : static_cast<char>('1' + shortcutIndex);
    return std::string("Ctrl+") + digit;
}

void runSearchResult(AppContext& context, const SearchUiApi& api, const SearchResult& result, LaunchItem& fallbackItem, int showCommand)
{
    if (result.pluginResult) {
        context.runPluginAction(*result.pluginResult, "open");
        return;
    }
    if (result.note) {
        if (api.openNote) {
            api.openNote(context, result.note->id);
        }
        return;
    }
    if (!api.runItem) {
        return;
    }
    if (result.globalFile && result.globalRecord) {
        fallbackItem = makeGlobalFileItem(*result.globalRecord);
        api.runItem(context, fallbackItem, showCommand);
        return;
    }
    if (result.item) {
        if (LaunchItem* liveItem = api.findItemById ? api.findItemById(context, result.item->id) : nullptr) {
            api.runItem(context, *liveItem, showCommand);
        }
    }
}

std::string noteSearchSubtitle(const Note& note)
{
    const std::string tags = formatNoteTags(note.tags);
    if (!tags.empty()) {
        return tags;
    }
    std::string body = note.body;
    body.erase(std::remove(body.begin(), body.end(), '\r'), body.end());
    std::replace(body.begin(), body.end(), '\n', ' ');
    if (body.size() > 160) {
        body.resize(160);
        body += "...";
    }
    return body;
}

void drawSearchResultMenu(const UiPalette& theme, AppContext& context, const SearchResult& result, LaunchItem& menuLaunchItem,
                          const SearchUiApi& api)
{
    const int popupOpacity = context.themes.active().popupMenuOpacity;
    const UiPalette popupTheme = withPopupOpacity(theme, popupOpacity);
    LightPopupStyle popupStyle(popupTheme, popupOpacity);
    const bool animatedPopup = ui_anim::pushPopupAppear("search-result-menu");
    if (ImGui::BeginPopup("search-result-menu")) {
        suppressCurrentViewportNativeBorder();
        if (result.pluginResult) {
            if (menuItem(popupTheme, Icons::Plugin, tr("Open"))) {
                context.runPluginAction(*result.pluginResult, "open");
                ImGui::CloseCurrentPopup();
            }
            if (!result.pluginResult->actions.empty()) {
                ImGui::Separator();
                for (const PluginAction& action : result.pluginResult->actions) {
                    if (action.id == "open") {
                        continue;
                    }
                    const std::string label = action.label.empty() ? action.id : action.label;
                    if (menuItem(popupTheme, Icons::RunAll, label.c_str())) {
                        context.runPluginAction(*result.pluginResult, action.id);
                        ImGui::CloseCurrentPopup();
                    }
                }
            }
            ImGui::Separator();
            if (beginIconMenu(popupTheme, Icons::CopyProperties, tr("Copy Properties"))) {
                if (menuItem(popupTheme, "", tr("Name")) && api.copyTextToClipboard) api.copyTextToClipboard(result.pluginResult->title);
                if (menuItem(popupTheme, "", tr("Remark")) && api.copyTextToClipboard)
                    api.copyTextToClipboard(result.pluginResult->subtitle);
                if (menuItem(popupTheme, "", "Plugin ID") && api.copyTextToClipboard)
                    api.copyTextToClipboard(result.pluginResult->pluginId);
                if (menuItem(popupTheme, "", "Result ID") && api.copyTextToClipboard) api.copyTextToClipboard(result.pluginResult->id);
                endIconMenu();
            }
            ImGui::EndPopup();
            if (animatedPopup) {
                ui_anim::popAppearAlpha();
            }
            return;
        }
        if (result.note) {
            if (menuItem(popupTheme, Icons::Note, tr("Open")) && api.openNote) {
                api.openNote(context, result.note->id);
                ImGui::CloseCurrentPopup();
            }
            ImGui::Separator();
            if (menuItem(popupTheme, Icons::CopyProperties, tr("Name")) && api.copyTextToClipboard)
                api.copyTextToClipboard(NotesStore::displayTitle(*result.note));
            if (menuItem(popupTheme, Icons::CopyProperties, tr("Remark")) && api.copyTextToClipboard)
                api.copyTextToClipboard(result.note->body);
            ImGui::EndPopup();
            if (animatedPopup) {
                ui_anim::popAppearAlpha();
            }
            return;
        }
        if (menuItem(popupTheme, Icons::Folder, tr("Open")) && api.runItem) {
            runSearchResult(context, api, result, menuLaunchItem, SW_SHOWNORMAL);
            ImGui::CloseCurrentPopup();
        }
        if (menuItem(popupTheme, Icons::RunAsAdmin, tr("Run as Administrator"), nullptr, false,
                     menuLaunchItem.type != LaunchItemType::VirtualFolder && menuLaunchItem.type != LaunchItemType::Note) &&
            api.runItem) {
            const bool old = menuLaunchItem.runAsAdmin;
            menuLaunchItem.runAsAdmin = true;
            if (result.globalFile || !result.item) {
                api.runItem(context, menuLaunchItem, SW_SHOWNORMAL);
            } else if (LaunchItem* liveItem = api.findItemById ? api.findItemById(context, result.item->id) : nullptr) {
                const bool liveOld = liveItem->runAsAdmin;
                liveItem->runAsAdmin = true;
                api.runItem(context, *liveItem, SW_SHOWNORMAL);
                liveItem->runAsAdmin = liveOld;
            }
            menuLaunchItem.runAsAdmin = old;
            ImGui::CloseCurrentPopup();
        }
        if (menuItem(popupTheme, Icons::MinimizeRun, tr("Run Minimized"), nullptr, false,
                     menuLaunchItem.type != LaunchItemType::VirtualFolder && menuLaunchItem.type != LaunchItemType::Note) &&
            api.runItem) {
            runSearchResult(context, api, result, menuLaunchItem, SW_SHOWMINIMIZED);
            ImGui::CloseCurrentPopup();
        }
        if (menuItem(popupTheme, Icons::OpenWith, tr("Open With"), nullptr, false,
                     menuLaunchItem.type != LaunchItemType::Note && !menuLaunchItem.target.empty()) &&
            api.openWithDialog) {
            api.openWithDialog(menuLaunchItem);
        }
        if (menuItem(popupTheme, Icons::Folder, tr("Open Containing Folder"), "Ctrl+Shift+E", false,
                     menuLaunchItem.type != LaunchItemType::Note && !menuLaunchItem.target.empty()) &&
            api.openContainingFolder) {
            api.openContainingFolder(menuLaunchItem);
        }
        if (menuItem(popupTheme, Icons::Folder, tr("Explorer Menu"), nullptr, false,
                     menuLaunchItem.type != LaunchItemType::Note && !menuLaunchItem.target.empty()) &&
            api.showFileProperties) {
            api.showFileProperties(menuLaunchItem);
        }
        if (result.globalFile && api.openGlobalFileEditor) {
            ImGui::Separator();
            if (menuItem(popupTheme, Icons::Add, tr("Add to Item List"))) {
                api.openGlobalFileEditor(context, menuLaunchItem);
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::Separator();
        if (beginIconMenu(popupTheme, Icons::CopyProperties, tr("Copy Properties"))) {
            if (menuItem(popupTheme, "", tr("All")) && api.itemPropertiesText && api.copyTextToClipboard)
                api.copyTextToClipboard(api.itemPropertiesText(menuLaunchItem));
            if (menuItem(popupTheme, "", tr("Name")) && api.copyTextToClipboard) api.copyTextToClipboard(menuLaunchItem.name);
            if (menuItem(popupTheme, "", tr("Target")) && api.copyTextToClipboard) api.copyTextToClipboard(menuLaunchItem.target.string());
            if (menuItem(popupTheme, "", tr("Start Directory")) && api.copyTextToClipboard)
                api.copyTextToClipboard(menuLaunchItem.startDirectory.string());
            if (menuItem(popupTheme, "", tr("Arguments")) && api.copyTextToClipboard) api.copyTextToClipboard(menuLaunchItem.arguments);
            if (menuItem(popupTheme, "", tr("Icon")) && api.copyTextToClipboard) api.copyTextToClipboard(menuLaunchItem.icon);
            if (menuItem(popupTheme, "", tr("Search Keywords")) && api.copyTextToClipboard)
                api.copyTextToClipboard(menuLaunchItem.keywords);
            if (menuItem(popupTheme, "", tr("Remark")) && api.copyTextToClipboard) api.copyTextToClipboard(menuLaunchItem.remark);
            endIconMenu();
        }
        if (menuItem(popupTheme, Icons::CopyPath, tr("Copy Path"), "Ctrl+Shift+C", false, !menuLaunchItem.target.empty()))
            ImGui::SetClipboardText(menuLaunchItem.target.string().c_str());
        if (!result.globalFile && api.copyItemToClipboard) {
            if (menuItem(popupTheme, Icons::Copy, tr("Copy"), "Ctrl+C")) api.copyItemToClipboard(menuLaunchItem, false);
            if (menuItem(popupTheme, Icons::Cut, tr("Cut"), "Ctrl+X")) api.copyItemToClipboard(menuLaunchItem, true);
        }
        if (menuItem(popupTheme, Icons::Paste, tr("Paste"), "Ctrl+V", false, api.clipboardAvailable ? api.clipboardAvailable() : false) &&
            api.pasteClipboardItem) {
            api.pasteClipboardItem(context);
        }
        ImGui::EndPopup();
    }
    if (animatedPopup) {
        ui_anim::popAppearAlpha();
    }
}

} // namespace

void drawSearchBar(const UiPalette& theme, AppContext& context, SearchUiState state, const ImVec2& origin, float width, float searchHeight)
{
    if (!state.focusSearch || !state.searchSubmit || !state.searchSelected || !state.searchMove || !state.searchPageMove ||
        !state.searchQueryText || !state.searchEditedAt) {
        return;
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(14.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
    const ImVec4 searchBg = ImGui::ColorConvertU32ToFloat4(theme.searchBar);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, searchBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, searchBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, searchBg);
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_NavCursor, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::GetWindowDrawList()->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + searchHeight), theme.searchBar);
    ImGui::SetNextItemWidth(width);
    const bool focusedThisFrame = *state.focusSearch;
    if (*state.focusSearch) {
        ImGui::SetKeyboardFocusHere();
        *state.focusSearch = false;
    }
    ImGui::SetNavCursorVisible(false);
    RuntimeState& runtime = context.runtime();
    const AppSettings& settings = context.persisted().settings;
    if (!focusedThisFrame && runtime.searchText.empty() && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        closeMainDockSearch(context);
        if (state.searchSubmit) {
            *state.searchSubmit = false;
        }
        ImGui::PopStyleColor(7);
        ImGui::PopStyleVar(3);
        return;
    }
    const std::string before = runtime.searchText;
    if (ImGui::InputTextWithHint("##search", tr("Search... keyword / qs / note / dir"), &runtime.searchText,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        *state.searchSubmit = true;
    }
    if (state.searchCursorEndRequested && *state.searchCursorEndRequested && ImGui::IsItemActive()) {
        if (ImGuiInputTextState* inputState = ImGui::GetInputTextState(ImGui::GetItemID())) {
            inputState->SetSelection(inputState->TextLen, inputState->TextLen);
            inputState->CursorFollow = true;
            inputState->CursorAnimReset();
        }
        *state.searchCursorEndRequested = false;
    }
    ImGui::SetNavCursorVisible(false);
    const ImRect searchFrame(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(searchFrame.Min, ImVec2(searchFrame.Max.x, searchFrame.Min.y), theme.searchBar, 2.0f);
    dl->AddLine(ImVec2(searchFrame.Min.x, searchFrame.Max.y), searchFrame.Max, theme.searchBar, 2.0f);
    dl->AddLine(searchFrame.Min, ImVec2(searchFrame.Min.x, searchFrame.Max.y), theme.searchBar, 2.0f);
    dl->AddLine(ImVec2(searchFrame.Max.x, searchFrame.Min.y), searchFrame.Max, theme.searchBar, 2.0f);
    if (ImGui::IsItemEdited() && before != runtime.searchText) {
        *state.searchSelected = 0;
        *state.searchEditedAt = ImGui::GetTime();
        if (!settings.smoothInput || settings.searchDelayMs <= 0) {
            *state.searchQueryText = runtime.searchText;
        }
    }
    if (settings.smoothInput && *state.searchQueryText != runtime.searchText &&
        (ImGui::GetTime() - *state.searchEditedAt) * 1000.0 >= settings.searchDelayMs) {
        *state.searchQueryText = runtime.searchText;
    }
    if (ImGui::IsItemActive()) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
            *state.searchMove = 1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
            *state.searchMove = -1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) {
            *state.searchPageMove = 1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) {
            *state.searchPageMove = -1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            closeMainDockSearch(context);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && runtime.searchText.empty() && runtime.selectedCategory >= 0 &&
                   runtime.selectedCategory < static_cast<int>(context.persisted().categories.size()) &&
                   context.persisted().categories.size() > 1) {
            requestDeleteCategory(context, runtime.selectedCategory);
            if (state.searchSubmit) {
                *state.searchSubmit = false;
            }
        }
    }
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(3);
}

void drawSearchResults(const UiPalette& theme, AppContext& context, SearchUiState state, const SearchUiApi& api, const ImVec2& origin,
                       const ImVec2& size)
{
    if (!state.searchSubmit || !state.searchSelected || !state.searchMove || !state.searchPageMove || !state.searchQueryText ||
        !api.findItemById || !api.runItem || !api.drawLaunchIcon) {
        return;
    }
    if (state.searchOpen && !*state.searchOpen) {
        return;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        closeMainDockSearch(context);
        return;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const RuntimeState& runtime = context.runtime();
    const AppSettings& settings = context.persisted().settings;
    const bool hasBackgroundImage = context.themes.active().background.enabled && !context.themes.active().background.imagePath.empty();
    if (!hasBackgroundImage) {
        dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), theme.contentBg, theme.windowRounding,
                          ImDrawFlags_RoundCornersBottom);
    }
    const std::string queryText = (settings.smoothInput && settings.searchDelayMs > 0) ? *state.searchQueryText : runtime.searchText;
    if (runtime.searchText.empty()) {
        drawCenteredSearchMessage(theme, origin, size, tr("Type to search"), false);
        return;
    }

    const std::vector<SearchResult>& results = cachedSearchResults(context, state, queryText, settings);
    const bool currentResultsReady =
        searchResultsReadyFor(state, queryText, settings, context.persisted().scheduledTasks, context.search.revision());
    float declaredResultsHeight = 0.0f;
    if (currentResultsReady && !results.empty()) {
        std::vector<std::vector<size_t>> heightPages = paginateResults(results, settings);
        if (!heightPages.empty()) {
            int heightPage = state.searchResultsCache ? state.searchResultsCache->resultPage : 0;
            heightPage = std::clamp(heightPage, 0, static_cast<int>(heightPages.size()) - 1);
            declaredResultsHeight =
                2.0f + static_cast<float>(heightPages[static_cast<size_t>(heightPage)].size()) * kSearchResultRowHeight + 1.0f;
        }
    }

    const ImVec2 resultsChildSize(size.x, std::max(0.0f, size.y - kSearchFooterHeight));
    ImGui::SetCursorScreenPos(origin);
    const ImVec4 childBg(0.0f, 0.0f, 0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, childBg);
    ImGui::SetNextWindowContentSize(ImVec2(0.0f, declaredResultsHeight));
    ImGui::BeginChild("search-results", resultsChildSize, ImGuiChildFlags_None, ImGuiWindowFlags_AlwaysVerticalScrollbar);
    dl = ImGui::GetWindowDrawList();
    if (gSearchScrollTopRequested) {
        ImGui::SetScrollY(0.0f);
        gSearchScrollTopRequested = false;
    }
    if (!currentResultsReady) {
        if (state.searchResultsCache) {
            state.searchResultsCache->resultPage = 0;
            state.searchResultsCache->resultPageJump = 1;
            state.searchResultsCache->selectionFromMouse = false;
        }
        *state.searchSelected = 0;
        *state.searchSubmit = false;
        *state.searchMove = 0;
        *state.searchPageMove = 0;
        std::string loadingTextStorage;
        const char* loadingText = tr("Searching...");
        if (settings.enableGlobalSearch) {
            const GlobalIndexProgress progress = context.globalFiles.progress();
            if (context.globalFiles.indexing() || progress.active) {
                loadingTextStorage = tr("Loading search index...");
                if (progress.totalRoots > 0) {
                    loadingTextStorage += " (";
                    loadingTextStorage += std::to_string(progress.completedRoots);
                    loadingTextStorage += "/";
                    loadingTextStorage += std::to_string(progress.totalRoots);
                    loadingTextStorage += ")";
                }
                if (progress.indexedFiles > 0) {
                    loadingTextStorage += " ";
                    loadingTextStorage += std::to_string(progress.indexedFiles);
                    loadingTextStorage += " ";
                    loadingTextStorage += tr("files");
                }
                if (progress.totalRoots > 0) {
                    const float rootBase = static_cast<float>(progress.completedRoots) / static_cast<float>(progress.totalRoots);
                    const float rootSpan = 1.0f / static_cast<float>(progress.totalRoots);
                    const float fraction = rootBase + rootSpan * std::clamp(progress.currentRootFraction, 0.0f, 1.0f);
                    char percent[32]{};
                    std::snprintf(percent, sizeof(percent), " %.0f%%", std::clamp(fraction, 0.0f, 1.0f) * 100.0f);
                    loadingTextStorage += percent;
                }
                loadingText = loadingTextStorage.c_str();
            }
        }
        drawCenteredSearchMessage(theme, origin, resultsChildSize, loadingText, true);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }
    if (results.empty()) {
        if (state.searchResultsCache) {
            state.searchResultsCache->resultPage = 0;
            state.searchResultsCache->resultPageJump = 1;
            state.searchResultsCache->selectionFromMouse = false;
        }
        *state.searchSelected = 0;
        *state.searchSubmit = false;
        *state.searchMove = 0;
        *state.searchPageMove = 0;
        drawCenteredSearchMessage(theme, origin, resultsChildSize, tr("No results"), false);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    std::vector<std::vector<size_t>> pages = paginateResults(results, settings);
    if (pages.empty()) {
        if (state.searchResultsCache) {
            state.searchResultsCache->resultPage = 0;
            state.searchResultsCache->resultPageJump = 1;
            state.searchResultsCache->selectionFromMouse = false;
        }
        *state.searchSelected = 0;
        *state.searchSubmit = false;
        *state.searchMove = 0;
        *state.searchPageMove = 0;
        drawCenteredSearchMessage(theme, origin, resultsChildSize, tr("No results"), false);
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    int fallbackPage = 0;
    int fallbackPageJump = 1;
    bool fallbackSelectionFromMouse = false;
    int& resultPage = state.searchResultsCache ? state.searchResultsCache->resultPage : fallbackPage;
    int& resultPageJump = state.searchResultsCache ? state.searchResultsCache->resultPageJump : fallbackPageJump;
    bool& selectionFromMouse = state.searchResultsCache ? state.searchResultsCache->selectionFromMouse : fallbackSelectionFromMouse;
    resultPage = std::clamp(resultPage, 0, static_cast<int>(pages.size()) - 1);
    resultPageJump = std::clamp(resultPageJump, 1, static_cast<int>(pages.size()));

    const bool movedSelection = *state.searchMove != 0;
    if (movedSelection) {
        int nextSelection = *state.searchSelected;
        if (nextSelection < 0) {
            nextSelection = *state.searchMove > 0 ? 0 : static_cast<int>(pages[static_cast<size_t>(resultPage)].size()) - 1;
        } else {
            nextSelection += *state.searchMove;
        }
        if (nextSelection < 0 && resultPage > 0) {
            --resultPage;
            resultPageJump = resultPage + 1;
            nextSelection = static_cast<int>(pages[static_cast<size_t>(resultPage)].size()) - 1;
        } else if (nextSelection >= static_cast<int>(pages[static_cast<size_t>(resultPage)].size()) &&
                   resultPage + 1 < static_cast<int>(pages.size())) {
            ++resultPage;
            resultPageJump = resultPage + 1;
            nextSelection = 0;
        }
        *state.searchSelected = std::clamp(nextSelection, 0, static_cast<int>(pages[static_cast<size_t>(resultPage)].size()) - 1);
        selectionFromMouse = false;
        *state.searchMove = 0;
    } else {
        if (*state.searchSelected >= 0) {
            *state.searchSelected =
                std::clamp(*state.searchSelected, 0, static_cast<int>(pages[static_cast<size_t>(resultPage)].size()) - 1);
        }
    }

    const std::vector<size_t>* pageItems = &pages[static_cast<size_t>(resultPage)];
    if (*state.searchPageMove != 0) {
        const int pageCount = static_cast<int>(pages.size());
        const int nextPage = std::clamp(resultPage + *state.searchPageMove, 0, pageCount - 1);
        if (nextPage != resultPage) {
            resultPage = nextPage;
            resultPageJump = resultPage + 1;
            *state.searchSelected = 0;
            selectionFromMouse = false;
            pageItems = &pages[static_cast<size_t>(resultPage)];
            ImGui::SetScrollY(0.0f);
        }
        *state.searchPageMove = 0;
    }
    if (settings.searchAltNumberRun && ImGui::GetIO().KeyAlt) {
        for (int i = 0; i < std::min<int>(9, pageItems->size()); ++i) {
            if (ImGui::IsKeyPressed(static_cast<ImGuiKey>(ImGuiKey_1 + i), false)) {
                const SearchResult& result = results[(*pageItems)[static_cast<size_t>(i)]];
                LaunchItem fallbackItem;
                runSearchResult(context, api, result, fallbackItem, SW_SHOWNORMAL);
                ImGui::EndChild();
                ImGui::PopStyleColor();
                return;
            }
        }
    }

    if (*state.searchSubmit) {
        const int selectedIndex = *state.searchSelected >= 0 ? *state.searchSelected : 0;
        const SearchResult& selected = results[(*pageItems)[static_cast<size_t>(selectedIndex)]];
        LaunchItem fallbackItem;
        runSearchResult(context, api, selected, fallbackItem, SW_SHOWNORMAL);
        *state.searchSubmit = false;
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    float rowStartY = 2.0f;
    const int pageCount = static_cast<int>(pages.size());
    const bool showPages = pageCount > 1;
    const float scrollViewportHeight = std::max(0.0f, ImGui::GetContentRegionAvail().y);
    auto pageRowsOverflowViewport = [&](const std::vector<size_t>& items) {
        return static_cast<float>(items.size()) * kSearchResultRowHeight > scrollViewportHeight;
    };
    bool showJumpButtons = pageRowsOverflowViewport(*pageItems);

    if (movedSelection && *state.searchSelected >= 0 && *state.searchSelected < static_cast<int>(pageItems->size())) {
        const float viewportHeight = std::max(0.0f, ImGui::GetWindowHeight() - rowStartY);
        const float selectedTop = rowStartY + static_cast<float>(*state.searchSelected) * kSearchResultRowHeight;
        const float selectedBottom = selectedTop + kSearchResultRowHeight;
        const float visibleTop = ImGui::GetScrollY();
        const float visibleBottom = visibleTop + viewportHeight;
        if (selectedTop < visibleTop || selectedBottom > visibleBottom) {
            ImGui::SetScrollY(std::max(0.0f, selectedTop - std::max(0.0f, (viewportHeight - kSearchResultRowHeight) * 0.5f)));
        }
    }

    ImGui::SetCursorPos(ImVec2(0.0f, rowStartY));
    bool anyRowHovered = false;
    int visibleShortcutCount = 0;
    const ImGuiIO& io = ImGui::GetIO();
    const bool ctrlShortcutActive = io.KeyCtrl;
    const bool mouseMoved = (io.MouseDelta.x * io.MouseDelta.x + io.MouseDelta.y * io.MouseDelta.y) > 0.01f;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(pageItems->size()), kSearchResultRowHeight);
    while (clipper.Step()) {
        for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
            const SearchResult& result = results[(*pageItems)[static_cast<size_t>(index)]];
            LaunchItem globalDisplayItem;
            const LaunchItem* displayItem = result.item;
            std::string globalName;
            std::string globalPath;
            if (result.globalFile && result.globalRecord) {
                globalName = std::string(globalFileName(*result.globalRecord));
                globalPath = globalFilePath(*result.globalRecord);
            }
            const PluginSearchResult* pluginResult = result.pluginResult.get();
            if (!displayItem && !result.note && !pluginResult && globalName.empty()) {
                continue;
            }
            const std::string pluginRowId = pluginResult ? (pluginResult->pluginId + ":" + pluginResult->id) : std::string{};
            ImGui::PushID(displayItem
                              ? displayItem->id.c_str()
                              : (result.note ? result.note->id.c_str() : (pluginResult ? pluginRowId.c_str() : globalPath.c_str())));
            const ImVec2 row = ImGui::GetCursorScreenPos();
            const float rowWidth = ImGui::GetContentRegionAvail().x;
            ImGui::InvisibleButton("row", ImVec2(rowWidth, kSearchResultRowHeight));
            const bool rowVisible = ImGui::IsItemVisible();
            const bool hovered = ImGui::IsItemHovered();
            if (hovered) {
                anyRowHovered = true;
            }
            if (hovered && (mouseMoved || ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
                *state.searchSelected = index;
                selectionFromMouse = true;
            }
            const bool keyboardSelected = !selectionFromMouse && index == *state.searchSelected;
            const float highlightAmount = ui_anim::hoverAmount(ImGui::GetID("search-row-highlight"), hovered || keyboardSelected, 0.14f);
            if (highlightAmount > 0.01f || keyboardSelected) {
                constexpr float highlightInsetX = 8.0f;
                constexpr float highlightInsetY = 3.0f;
                constexpr float highlightRounding = 7.0f;
                const ImVec2 highlightMin(row.x + highlightInsetX, row.y + highlightInsetY);
                const ImVec2 highlightMax(row.x + rowWidth - highlightInsetX, row.y + kSearchResultRowHeight - highlightInsetY);
                dl->AddRectFilled(highlightMin, highlightMax, searchRowHighlight(theme, keyboardSelected, highlightAmount),
                                  highlightRounding);
            }
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                runSearchResult(context, api, result, globalDisplayItem, SW_SHOWNORMAL);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                *state.searchSelected = index;
                selectionFromMouse = false;
                ImGui::OpenPopup("search-result-menu");
            }
            if (result.globalFile && result.globalRecord) {
                globalDisplayItem = makeGlobalFileItem(*result.globalRecord);
                const ImVec2 iconPos(row.x + 16.0f, row.y + 14.0f);
                bool drewIcon = false;
                if (ImGui::IsItemVisible() && api.drawCachedLaunchIcon) {
                    drewIcon = api.drawCachedLaunchIcon(globalDisplayItem, iconPos, 28.0f);
                    if (!drewIcon && api.requestLaunchIcon) {
                        api.requestLaunchIcon(globalDisplayItem);
                    }
                }
                if (!drewIcon) {
                    drawGenericSearchIcon(dl, theme, ImVec2(row.x + 16.0f, row.y + 14.0f), 28.0f, result.globalRecord->directory);
                }
            } else if (result.note) {
                drawGenericSearchIcon(dl, theme, ImVec2(row.x + 16.0f, row.y + 14.0f), 28.0f, false);
                const ImVec2 iconPos(row.x + 22.0f, row.y + 18.0f);
                dl->AddText(nullptr, 18.0f, iconPos, theme.text, Icons::Note);
            } else if (pluginResult) {
                drawPluginSearchIcon(dl, theme, ImVec2(row.x + 16.0f, row.y + 14.0f), 28.0f, pluginResult->icon);
            } else {
                api.drawLaunchIcon(*displayItem, ImVec2(row.x + 16.0f, row.y + 14.0f), 28.0f);
            }
            const ImVec2 namePos(row.x + 56.0f, row.y + 8.0f);
            const float textRight = row.x + rowWidth - 12.0f;
            const ImVec4 nameClip(namePos.x, row.y, textRight, row.y + 30.0f);
            const ImVec4 pathClip(namePos.x, row.y + 24.0f, textRight, row.y + kSearchResultRowHeight);
            const std::string nameText = result.globalFile ? globalName
                                         : result.note     ? NotesStore::displayTitle(*result.note)
                                         : pluginResult    ? pluginResult->title
                                                           : displayItem->name;
            addClippedText(dl, namePos, theme.text, nameText, nameClip);
            const float nameWidth = std::min(ImGui::CalcTextSize(nameText.c_str()).x, std::max(0.0f, textRight - namePos.x - 120.0f));
            const std::string categoryName =
                result.globalFile ? tr("Global Search")
                                  : (result.note ? tr("Notes")
                                                 : (pluginResult ? pluginDisplayName(context, pluginResult->pluginId)
                                                                 : (result.category ? result.category->name : std::string{})));
            const std::string categoryText = " [" + categoryName + "]";
            addClippedText(dl, ImVec2(namePos.x + nameWidth + 4.0f, namePos.y), theme.textMuted, categoryText, nameClip);
            const std::string pathText = result.globalFile ? globalPath
                                         : result.note     ? noteSearchSubtitle(*result.note)
                                         : pluginResult    ? pluginResult->subtitle
                                                           : displayItem->target.string();
            addClippedText(dl, ImVec2(namePos.x, row.y + 30.0f), theme.textMuted, pathText, pathClip);
            if (rowVisible && visibleShortcutCount < 10) {
                const int shortcutIndex = visibleShortcutCount++;
                const std::string shortcut = searchShortcutText(shortcutIndex);
                const ImVec2 shortcutSize = ImGui::CalcTextSize(shortcut.c_str());
                addClippedText(dl, ImVec2(textRight - shortcutSize.x, row.y + 8.0f), theme.textMuted, shortcut, nameClip);
                if (ctrlShortcutActive && ImGui::IsKeyPressed(searchShortcutKey(shortcutIndex), false)) {
                    *state.searchSelected = index;
                    selectionFromMouse = false;
                }
            }
            LaunchItem menuLaunchItem = result.globalFile ? globalDisplayItem : (displayItem ? *displayItem : LaunchItem{});
            drawSearchResultMenu(theme, context, result, menuLaunchItem, api);
            ImGui::PopID();
        }
    }
    clipper.End();
    const float listContentHeight = rowStartY + static_cast<float>(pageItems->size()) * kSearchResultRowHeight;
    if (ImGui::GetCursorPosY() < listContentHeight) {
        ImGui::SetCursorPosY(listContentHeight);
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
    }
    if (!anyRowHovered && selectionFromMouse) {
        *state.searchSelected = -1;
        selectionFromMouse = false;
    }
    showJumpButtons = pageRowsOverflowViewport(*pageItems);
    if (gSearchScrollBottomRequested) {
        ImGui::SetScrollY(ImGui::GetScrollMaxY());
        gSearchScrollBottomRequested = false;
    }
    ImGui::EndChild();

    if (!pageItems->empty() && (showPages || showJumpButtons)) {
        setSearchFooterCursor(origin, size, searchBottomControlsWidth(showPages, showJumpButtons, resultPage, pageCount));
        if (showPages) {
            if (drawSearchPaginationControls("footer-pages", resultPage, resultPageJump, pageCount, state.searchSelected)) {
                selectionFromMouse = false;
            }
            if (showJumpButtons) {
                ImGui::SameLine();
            }
        }
        if (showJumpButtons && ImGui::Button(tr("Top"), ImVec2(0.0f, ImGui::GetFrameHeight()))) {
            gSearchScrollTopRequested = true;
        }
        if (showJumpButtons) {
            ImGui::SameLine();
            if (ImGui::Button(tr("Bottom"), ImVec2(0.0f, ImGui::GetFrameHeight()))) {
                gSearchScrollBottomRequested = true;
            }
        }
    }
    ImGui::PopStyleColor();
}

} // namespace launcher
