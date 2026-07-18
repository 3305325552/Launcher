#include "ui/dock/MainDock.hpp"

#include "app/AppContext.hpp"
#include "core/LaunchParameterUtils.hpp"
#include "core/ModelActions.hpp"
#include "launcher/AppIdentity.hpp"
#include "ui/settings/ConfigTransfer.hpp"
#include "ui/common/Localization.hpp"
#include "ui/dock/MainDockCategoryRail.hpp"
#include "ui/common/UiChrome.hpp"
#include "ui/dock/MainDockContextMenus.hpp"
#include "ui/dock/MainDockDialogs.hpp"
#include "ui/dock/MainDockGrid.hpp"
#include "ui/dock/MainDockItemEditor.hpp"
#include "ui/dock/MainDockItemViews.hpp"
#include "ui/dock/MainDockMenu.hpp"
#include "ui/dock/MainDockNavigation.hpp"
#include "ui/dock/MainDockNotes.hpp"
#include "ui/dock/MainDockUiState.hpp"
#include "ui/rendering/MainDockResources.hpp"
#include "ui/dock/MainDockSearch.hpp"
#include "ui/dock/MainDockState.hpp"
#include "ui/platform/UiPlatform.hpp"
#include "ui/common/MaterialIcons.hpp"
#include "ui/settings/SettingsPanel.hpp"
#include "ui/settings/ThemeEditor.hpp"
#include "ui/common/UiAnimation.hpp"
#include "ui/common/UiTheme.hpp"
#include "ui/views/UserGuideView.hpp"

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace launcher {
namespace {

using model_actions::findItemInList;
using model_actions::itemIndexById;
using launch_params::HistoryCandidate;
using launch_params::defaultParamValue;
using launch_params::effectiveParamId;
using launch_params::interactiveHistoryCandidates;
using launch_params::interactiveParamKey;
using launch_params::itemNeedsInteractivePrompt;
using launch_params::removeInteractiveHistoryValue;
using launch_params::withInteractiveValues;
using launch_params::withSearchVariables;

constexpr float kTitleHeight = kUiTitleHeight;
constexpr float kSearchHeight = 36.0f;
constexpr float kRailWidth = 164.0f;
constexpr int kMinWindowWidth = 720;
constexpr int kMinWindowHeight = 520;

MainDockSession gSession;
MainDockResources gResources;

void resetIconLoadScheduling()
{
    gResources.resetIconLoadScheduling();
}

void clearIconTextureCache()
{
    gResources.clearIcons();
}

void beginIconLoadFrame(const AppContext& context)
{
    gResources.beginIconLoadFrame(context, gSession.searchOpen, gSession.useDefaultIcons, gSession.searchQueryText.c_str());
}

bool drawCachedLaunchIcon(const LaunchItem& item, const ImVec2& pos, float size)
{
    return gResources.drawCachedLaunchIcon(item, pos, size, gSession.useDefaultIcons);
}

void requestLaunchIcon(const LaunchItem& item)
{
    gResources.requestLaunchIcon(item, gSession.useDefaultIcons);
}

void processPendingIconRequests()
{
    gResources.processPendingIconRequests(gSession.useDefaultIcons);
}

void clearIconCacheForItem(const LaunchItem& item)
{
    gResources.clearIconForItem(item);
}

std::int64_t nowUnix()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string timeText(std::int64_t value)
{
    if (value <= 0) {
        return "-";
    }
    std::time_t time = static_cast<std::time_t>(value);
    std::tm local{};
    localtime_s(&local, &time);
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
    return buffer;
}

void openInteractiveRunPrompt(const LaunchItem& item, int showCommand, const std::string& searchText)
{
    gSession.interactiveRunItem = item;
    gSession.interactiveRunItemId = item.id;
    gSession.interactiveRunSearchText = searchText;
    gSession.interactiveRunShowCommand = showCommand;
    gSession.interactiveRunValues.clear();
    gSession.interactiveRunValues.reserve(item.interactiveParams.size());
    gSession.interactiveHistoryParamKey.clear();
    gSession.interactiveHistorySelected = -1;
    for (const InteractiveParam& param : item.interactiveParams) {
        gSession.interactiveRunValues.push_back(defaultParamValue(param));
    }
    gSession.showInteractiveRun = true;
    gSession.openInteractiveRunPopup = true;
}

Category* selectedCategory(AppContext& context)
{
    if (context.runtime().selectedCategory < 0 ||
        context.runtime().selectedCategory >= static_cast<int>(context.persisted().categories.size())) {
        return nullptr;
    }
    return &context.persisted().categories[context.runtime().selectedCategory];
}

bool hasThemeBackground(const ThemeDefinition& theme)
{
    return gResources.hasBackground(theme);
}

bool removeItemFromList(std::vector<LaunchItem>& items, const std::unordered_set<std::string>& pending)
{
    bool changed = false;
    for (LaunchItem& item : items) {
        if (removeItemFromList(item.children, pending)) {
            changed = true;
        }
    }
    const auto oldSize = items.size();
    items.erase(std::remove_if(items.begin(), items.end(),
                               [&](const LaunchItem& item) {
                                   return pending.contains(item.id);
                               }),
                items.end());
    return changed || items.size() != oldSize;
}

LaunchItem* findItemById(AppContext& context, const std::string& id)
{
    for (Category& category : context.persisted().categories) {
        if (LaunchItem* item = findItemInList(category.items, id)) {
            return item;
        }
    }
    return nullptr;
}

const LaunchItem* findItemById(const AppContext& context, const std::string& id)
{
    for (const Category& category : context.persisted().categories) {
        if (const LaunchItem* item = findItemInList(category.items, id)) {
            return item;
        }
    }
    return nullptr;
}

std::vector<LaunchItem>* currentItems(AppContext& context)
{
    Category* category = selectedCategory(context);
    if (!category) {
        return nullptr;
    }
    std::vector<LaunchItem>* items = &category->items;
    std::vector<std::string> validStack;
    for (const std::string& id : context.runtime().currentFolderStack) {
        LaunchItem* folder = findItemInList(*items, id);
        if (!folder || folder->type != LaunchItemType::VirtualFolder) {
            break;
        }
        validStack.push_back(id);
        items = &folder->children;
    }
    if (validStack.size() != context.runtime().currentFolderStack.size()) {
        context.runtime().currentFolderStack = std::move(validStack);
    }
    return items;
}

const std::vector<LaunchItem>* currentItems(const AppContext& context)
{
    auto& mutableContext = const_cast<AppContext&>(context);
    return currentItems(mutableContext);
}

std::vector<LaunchItem>* itemsForFolderStack(AppContext& context, const std::vector<std::string>& stack)
{
    Category* category = selectedCategory(context);
    if (!category) {
        return nullptr;
    }
    std::vector<LaunchItem>* items = &category->items;
    for (const std::string& id : stack) {
        LaunchItem* folder = findItemInList(*items, id);
        if (!folder || folder->type != LaunchItemType::VirtualFolder) {
            return nullptr;
        }
        items = &folder->children;
    }
    return items;
}

std::string dragPayloadId(const ImGuiPayload* payload)
{
    if (!payload || payload->DataSize <= 1) {
        return {};
    }
    return std::string(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize - 1));
}

std::string stackKey(const std::vector<std::string>& stack)
{
    std::string key = "folder:";
    for (const std::string& id : stack) {
        key += id;
        key += "/";
    }
    return key;
}

ImVec4 withAlpha(ImU32 color, float alphaMultiplier)
{
    ImVec4 result = ImGui::ColorConvertU32ToFloat4(color);
    result.w = std::clamp(result.w * alphaMultiplier, 0.0f, 1.0f);
    return result;
}

std::vector<LaunchItem>* editingItems(AppContext& context)
{
    if (gSession.editingFolderId.empty()) {
        if (gSession.editingCategory < 0 || gSession.editingCategory >= static_cast<int>(context.persisted().categories.size())) {
            return nullptr;
        }
        return &context.persisted().categories[gSession.editingCategory].items;
    }
    for (Category& category : context.persisted().categories) {
        if (LaunchItem* folder = findItemInList(category.items, gSession.editingFolderId)) {
            return &folder->children;
        }
    }
    return nullptr;
}

int dropInsertIndex(int targetIndex, const ImRect& rect)
{
    const bool after = ImGui::GetMousePos().y > (rect.Min.y + rect.Max.y) * 0.5f;
    return targetIndex + (after ? 1 : 0);
}

void reorderCategory(AppContext& context, int from, int insertAt)
{
    if (model_actions::reorderCategory(context.persisted(), context.runtime(), from, insertAt)) {
        context.commitContentChange();
    }
}

void reorderItem(AppContext& context, std::vector<LaunchItem>& items, int from, int insertAt)
{
    if (model_actions::reorderItem(items, context.persisted().settings, currentListLayoutLocked(context), from, insertAt)) {
        context.commitContentChange();
    }
}

std::string makeId(const std::string& name, int index)
{
    return name + "-" + std::to_string(index + 1) + "-" + std::to_string(nowUnix());
}

LaunchItem makeNewItem(const std::string& name, LaunchItemType type, int index)
{
    LaunchItem item;
    item.id = makeId("item", index);
    item.name = name;
    item.type = type;
    item.createdAt = nowUnix();
    item.lastEditedAt = item.createdAt;
    if (type == LaunchItemType::Url) {
        item.target = "https://";
        item.subtitle = "Url";
        item.remark = "Url";
    } else if (type == LaunchItemType::Script) {
        item.subtitle = "Script";
        item.remark = "Script";
    } else if (type == LaunchItemType::VirtualFolder) {
        item.subtitle = "Virtual folder";
        item.remark = "Virtual folder";
    } else if (type == LaunchItemType::Note) {
        item.subtitle = tr("Notes");
        item.remark = tr("Notes");
        item.fallbackColor = "#6A9A7CFF";
    }
    return item;
}

void openItemEditor(AppContext& context, int itemIndex, LaunchItemType type = LaunchItemType::App)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }

    gSession.editingCategory = context.runtime().selectedCategory;
    gSession.editingFolderId = context.runtime().currentFolderStack.empty() ? "" : context.runtime().currentFolderStack.back();
    gSession.editingItem = itemIndex;
    if (itemIndex >= 0 && itemIndex < static_cast<int>(items->size())) {
        gSession.editingDraft = (*items)[itemIndex];
    } else {
        const char* defaultName = type == LaunchItemType::Title           ? "Title"
                                  : type == LaunchItemType::VirtualFolder ? "Virtual Folder"
                                  : type == LaunchItemType::Note          ? tr("Note")
                                                                          : "New Item";
        gSession.editingDraft = makeNewItem(defaultName, type, static_cast<int>(items->size()));
    }
    gSession.editingTarget = gSession.editingDraft.target.string();
    gSession.editingStartDir = gSession.editingDraft.startDirectory.string();
    gSession.editingRemark = gSession.editingDraft.remark.empty() ? gSession.editingDraft.subtitle : gSession.editingDraft.remark;
    gSession.editingIcon = gSession.editingDraft.icon;
    gSession.showItemEditor = true;
    gSession.openItemEditorPopup = true;
}

void openItemEditorWithDraft(AppContext& context, const LaunchItem& sourceItem)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }

    LaunchItem item = sourceItem;
    item.id = makeId("item", static_cast<int>(items->size()));
    item.createdAt = nowUnix();
    item.lastEditedAt = item.createdAt;
    gSession.editingCategory = context.runtime().selectedCategory;
    gSession.editingFolderId = context.runtime().currentFolderStack.empty() ? "" : context.runtime().currentFolderStack.back();
    gSession.editingItem = -1;
    gSession.editingDraft = std::move(item);
    gSession.editingTarget = gSession.editingDraft.target.string();
    gSession.editingStartDir = gSession.editingDraft.startDirectory.string();
    gSession.editingRemark = gSession.editingDraft.remark.empty() ? gSession.editingDraft.subtitle : gSession.editingDraft.remark;
    gSession.editingIcon = gSession.editingDraft.icon;
    gSession.showItemEditor = true;
    gSession.openItemEditorPopup = true;
}

void appendItem(AppContext& context, LaunchItemType type, const std::string& name)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }
    LaunchItem item = makeNewItem(name, type, static_cast<int>(items->size()));
    items->push_back(item);
    selectSingle(context, items->back());
    context.commitContentChange();
}

void appendNoteItem(AppContext& context)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }

    Note& note = context.notes.createNote(tr("Untitled Note"));
    LaunchItem item = makeNewItem(NotesStore::displayTitle(note), LaunchItemType::Note, static_cast<int>(items->size()));
    item.id = "note-item-" + note.id + "-" + std::to_string(nowUnix());
    item.target = note.id;
    item.subtitle = tr("Notes");
    item.remark = tr("Notes");
    items->push_back(std::move(item));
    selectSingle(context, items->back());
    context.runtime().selectedNoteId = note.id;
    context.runtime().showNotes = true;
    context.runtime().showNoteQuick = false;
    context.commitContentChange();
}

bool reorderItemById(AppContext& context, std::vector<LaunchItem>& items, const std::string& id, int insertAt)
{
    const int from = itemIndexById(items, id);
    if (from < 0) {
        return false;
    }
    reorderItem(context, items, from, insertAt);
    return true;
}

bool moveItemByIdToList(AppContext& context, const std::string& id, std::vector<LaunchItem>& destination, int insertAt = -1)
{
    const model_actions::MoveItemsResult result = model_actions::moveItemByIdToList(context.persisted(), id, destination, insertAt);
    if (!result.changed) {
        return false;
    }
    if (result.movedAcrossLists) {
        clearSelection(context);
    }
    context.commitContentChange();
    return true;
}

bool moveItemIdsToList(AppContext& context, const std::vector<std::string>& ids, std::vector<LaunchItem>& destination, int insertAt = -1)
{
    const model_actions::MoveItemsResult result = model_actions::moveItemIdsToList(context.persisted(), ids, destination, insertAt);
    if (!result.changed) {
        return false;
    }
    if (result.movedIds.size() > 1) {
        const std::string activeId =
            std::find(result.movedIds.begin(), result.movedIds.end(), context.runtime().selectedItemId) != result.movedIds.end()
                ? context.runtime().selectedItemId
                : result.movedIds.front();
        selectIds(context, result.movedIds, activeId);
    } else if (result.movedAcrossLists) {
        clearSelection(context);
    }
    context.commitContentChange();
    return true;
}

bool isFolderHoveredForAutoEnter(const LaunchItem& item, const ImVec2& tile, float tileW, float tileH)
{
    if (item.type != LaunchItemType::VirtualFolder) {
        return false;
    }
    const ImRect centerZone(ImVec2(tile.x + tileW * 0.18f, tile.y + tileH * 0.14f), ImVec2(tile.x + tileW * 0.82f, tile.y + tileH * 0.86f));
    return centerZone.Contains(ImGui::GetIO().MousePos);
}

void requestHideMainWindow()
{
    if (HWND hwnd = mainWindowHandle()) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

void resetSearchState(AppContext& context)
{
    gSession.focusSearch = false;
    gSession.searchOpen = false;
    gSession.searchSubmit = false;
    gSession.searchSelected = 0;
    gSession.searchMove = 0;
    gSession.searchPageMove = 0;
    gSession.searchEditedAt = 0.0;
    gSession.searchQueryText.clear();
    gSession.searchResultsCache.results.clear();
    gSession.searchResultsCache.query.clear();
    gSession.searchResultsCache.settingsKey.clear();
    gSession.searchResultsCache.indexRevision = 0;
    gSession.searchResultsCache.valid = false;
    context.runtime().searchText.clear();
}

bool hasOpenManagedWindow(const AppContext& context)
{
    return context.runtime().showSettings || context.runtime().showThemeEditor || gSession.showItemEditor || gSession.showTaskPlanner ||
           gSession.showBuildInfo || gSession.editingCategoryIndex >= 0 || hasPendingDeleteState() ||
           ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
}

void snapMainWindowIfNeeded(const AppSettings& settings)
{
    if ((!settings.magneticScreenCorner && !settings.magneticScreenEdge) || settings.lockWindowPosition) {
        return;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        return;
    }
    HWND hwnd = mainWindowHandle();
    if (!hwnd || !IsWindowVisible(hwnd)) {
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
    constexpr int threshold = 36;
    int x = rect.left;
    int y = rect.top;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    bool changed = false;

    const bool rectNearLeft = rect.left <= info.rcWork.left + threshold;
    const bool rectNearRight = rect.right >= info.rcWork.right - threshold;
    const bool rectNearTop = rect.top <= info.rcWork.top + threshold;
    const bool rectNearBottom = rect.bottom >= info.rcWork.bottom - threshold;
    if (settings.magneticScreenCorner && ((rectNearLeft || rectNearRight) && (rectNearTop || rectNearBottom))) {
        if (rectNearLeft) {
            x = info.rcWork.left;
            changed = true;
        }
        if (rectNearRight) {
            x = info.rcWork.right - width;
            changed = true;
        }
        if (rectNearTop) {
            y = info.rcWork.top;
            changed = true;
        }
        if (rectNearBottom) {
            y = info.rcWork.bottom - height;
            changed = true;
        }
    } else if (settings.magneticScreenEdge) {
        if (rectNearLeft) {
            x = info.rcWork.left;
            changed = true;
        } else if (rectNearRight) {
            x = info.rcWork.right - width;
            changed = true;
        }
        if (rectNearTop) {
            y = info.rcWork.top;
            changed = true;
        } else if (rectNearBottom) {
            y = info.rcWork.bottom - height;
            changed = true;
        }
    }
    if (changed) {
        SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void appendPlaceholders(AppContext& context, int count)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        LaunchItem item = makeNewItem(" ", LaunchItemType::Placeholder, static_cast<int>(items->size()));
        items->push_back(item);
        selectSingle(context, items->back());
    }
    context.commitContentChange();
}

bool removeItemById(AppContext& context, const std::string& id)
{
    for (Category& category : context.persisted().categories) {
        std::unordered_set<std::string> ids{id};
        if (removeItemFromList(category.items, ids)) {
            return true;
        }
    }
    return false;
}

std::string itemTypeText(LaunchItemType type)
{
    switch (type) {
    case LaunchItemType::App: return "App";
    case LaunchItemType::Url: return "Url";
    case LaunchItemType::Script: return "Script";
    case LaunchItemType::BuiltIn: return "BuiltIn";
    case LaunchItemType::Placeholder: return "Placeholder";
    case LaunchItemType::Title: return "Title";
    case LaunchItemType::VirtualFolder: return "VirtualFolder";
    case LaunchItemType::Note: return "Note";
    }
    return "App";
}

std::string itemPropertiesText(const LaunchItem& item)
{
    std::string text;
    text += tr("Name: ") + item.name + "\n";
    text += tr("Type: ") + itemTypeText(item.type) + "\n";
    text += tr("Target: ") + item.target.string() + "\n";
    text += tr("Start directory: ") + item.startDirectory.string() + "\n";
    text += tr("Arguments: ") + item.arguments + "\n";
    text += tr("Icon: ") + item.icon + "\n";
    text += tr("Search keywords: ") + item.keywords + "\n";
    text += tr("Hotkey: ") + item.hotkey + "\n";
    text += tr("Remark: ") + item.remark + "\n";
    text += tr("Run count: ") + std::to_string(item.runCount);
    return text;
}

void enterVirtualFolder(AppContext& context, const LaunchItem& item);
void openNoteById(AppContext& context, const std::string& id);
void openNoteEditorById(AppContext& context, const std::string& id);
bool launchResolvedItem(AppContext& context, LaunchItem& sourceItem, LaunchItem launchItem, int showCommand);

void runItemsInList(AppContext& context, std::vector<LaunchItem>& items)
{
    for (LaunchItem& child : items) {
        if (child.type == LaunchItemType::VirtualFolder) {
            if (context.persisted().settings.virtualFolderRunsAll) {
                runItemsInList(context, child.children);
            }
            continue;
        }
        if (child.type == LaunchItemType::Title || child.type == LaunchItemType::Placeholder) {
            continue;
        }
        if (child.type == LaunchItemType::Note) {
            openNoteById(context, child.target.string());
            ++child.runCount;
            child.lastRunAt = nowUnix();
            continue;
        }
        LaunchItem launchItem =
            context.persisted().settings.searchParamVariable ? withSearchVariables(child, context.runtime().searchText) : child;
        launchResolvedItem(context, child, std::move(launchItem), SW_SHOWNORMAL);
    }
    context.save();
}

bool launchResolvedItem(AppContext& context, LaunchItem& sourceItem, LaunchItem launchItem, int showCommand)
{
    if (context.launcher.launch(launchItem, showCommand)) {
        ++sourceItem.runCount;
        sourceItem.lastRunAt = nowUnix();
        context.save();
        if (context.persisted().settings.closeSearchAfterRun) {
            resetSearchState(context);
        }
        if (context.persisted().settings.runItemHidesMain || context.persisted().settings.hideAfterRun) {
            requestHideMainWindow();
        }
        return true;
    }
    return false;
}

void runItem(AppContext& context, LaunchItem& item, int showCommand = SW_SHOWNORMAL)
{
    if (item.type == LaunchItemType::VirtualFolder) {
        enterVirtualFolder(context, item);
        return;
    }
    if (item.type == LaunchItemType::Title || item.type == LaunchItemType::Placeholder) {
        return;
    }
    if (item.type == LaunchItemType::Note) {
        openNoteById(context, item.target.string());
        ++item.runCount;
        item.lastRunAt = nowUnix();
        context.save();
        if (context.persisted().settings.closeSearchAfterRun) {
            resetSearchState(context);
        }
        return;
    }
    if (itemNeedsInteractivePrompt(item)) {
        const std::string searchText = context.persisted().settings.searchParamVariable ? context.runtime().searchText : std::string{};
        openInteractiveRunPrompt(item, showCommand, searchText);
        return;
    }
    LaunchItem launchItem =
        context.persisted().settings.searchParamVariable ? withSearchVariables(item, context.runtime().searchText) : item;
    launchResolvedItem(context, item, std::move(launchItem), showCommand);
}

void appendBuiltInItem(AppContext& context, const std::string& name, const std::string& target, const std::string& args = {})
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }
    LaunchItem item = makeNewItem(name, LaunchItemType::BuiltIn, static_cast<int>(items->size()));
    item.target = target;
    item.arguments = args;
    item.subtitle = "Built-in";
    item.remark = item.subtitle;
    items->push_back(item);
    selectSingle(context, items->back());
    context.commitContentChange();
}

ImU32 iconColor(const LaunchItem& item)
{
    auto hexDigit = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    auto hexByte = [&](const std::string& text, int offset, int fallback) -> int {
        if (offset + 1 >= static_cast<int>(text.size())) return fallback;
        const int hi = hexDigit(text[offset]);
        const int lo = hexDigit(text[offset + 1]);
        return hi < 0 || lo < 0 ? fallback : (hi << 4) | lo;
    };
    if (!item.fallbackColor.empty()) {
        std::string color = item.fallbackColor;
        if (color[0] == '#') {
            color.erase(color.begin());
        }
        if (color.size() == 6 || color.size() == 8) {
            const int r = hexByte(color, 0, 140);
            const int g = hexByte(color, 2, 140);
            const int b = hexByte(color, 4, 140);
            const int a = color.size() == 8 ? hexByte(color, 6, 255) : 255;
            return IM_COL32(r, g, b, a);
        }
    }
    if (item.type == LaunchItemType::Title) {
        return IM_COL32(150, 150, 150, 255);
    }
    if (item.type == LaunchItemType::Placeholder) {
        return IM_COL32(205, 205, 205, 255);
    }
    if (item.type == LaunchItemType::Note) {
        return IM_COL32(106, 154, 124, 255);
    }
    static constexpr std::array<ImU32, 8> palette = {IM_COL32(72, 136, 199, 255), IM_COL32(244, 119, 63, 255), IM_COL32(83, 174, 95, 255),
                                                     IM_COL32(94, 126, 220, 255), IM_COL32(233, 76, 96, 255),  IM_COL32(66, 165, 176, 255),
                                                     IM_COL32(244, 172, 50, 255), IM_COL32(140, 112, 206, 255)};
    return palette[std::hash<std::string>{}(item.id) % palette.size()];
}

void drawLaunchIconOnList(ImDrawList* dl, const LaunchItem& item, const ImVec2& pos, float size)
{
    const float rounding = item.type == LaunchItemType::Url ? size * 0.5f : 8.0f;
    if (item.type == LaunchItemType::VirtualFolder) {
        const float glyphFontSize = size * 0.82f;
        const ImVec2 glyphSize = ImGui::GetFont()->CalcTextSizeA(glyphFontSize, FLT_MAX, 0.0f, Icons::Folder);
        dl->AddText(nullptr, glyphFontSize, ImVec2(pos.x + (size - glyphSize.x) * 0.5f, pos.y + (size - glyphSize.y) * 0.5f),
                    iconColor(item), Icons::Folder);
        return;
    }
    if (item.type == LaunchItemType::Note) {
        dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), iconColor(item), rounding);
        const float glyphFontSize = size * 0.62f;
        const ImVec2 glyphSize = ImGui::GetFont()->CalcTextSizeA(glyphFontSize, FLT_MAX, 0.0f, Icons::Note);
        dl->AddText(nullptr, glyphFontSize, ImVec2(pos.x + (size - glyphSize.x) * 0.5f, pos.y + (size - glyphSize.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), Icons::Note);
        return;
    }
    if (gResources.drawLaunchIcon(dl, item, pos, size, gSession.useDefaultIcons)) {
        return;
    }

    dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), iconColor(item), rounding);
    if (item.type == LaunchItemType::Placeholder) {
        return;
    }
    std::string label = item.name.empty() ? "?" : item.name.substr(0, 1);
    const float fontSize = std::max(12.0f, size * 0.58f);
    const ImVec2 labelSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str());
    dl->AddText(nullptr, fontSize, ImVec2(pos.x + (size - labelSize.x) * 0.5f, pos.y + (size - labelSize.y) * 0.5f),
                IM_COL32(255, 255, 255, 255), label.c_str());
}

void drawLaunchIcon(const LaunchItem& item, const ImVec2& pos, float size)
{
    drawLaunchIconOnList(ImGui::GetWindowDrawList(), item, pos, size);
}

void drawConfiguredBackground(const AppContext& context, const ThemeDefinition& theme, const ImVec2& origin, const ImVec2& size)
{
    gResources.drawBackground(context, theme, origin, size);
}

bool mainViewportIsForeground()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return false;
    }
    auto* mainHwnd = static_cast<HWND>(viewport->PlatformHandleRaw);
    return mainHwnd && GetForegroundWindow() == mainHwnd;
}

bool mouseOverMainWindow()
{
    HWND main = mainWindowHandle();
    if (!main) {
        return false;
    }
    POINT pt{};
    GetCursorPos(&pt);
    HWND hit = WindowFromPoint(pt);
    if (!hit) {
        return false;
    }
    HWND root = GetAncestor(hit, GA_ROOT);
    return root == main || hit == main;
}

void selectAllCurrentCategory(AppContext& context);

void selectAllCurrentCategory(AppContext& context)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }
    std::vector<std::string> ids;
    ids.reserve(items->size());
    for (const LaunchItem& item : *items) {
        ids.push_back(item.id);
    }
    if (!items->empty()) {
        selectIds(context, ids, items->back().id);
    } else {
        clearSelection(context);
    }
}

ItemViewMode currentNavigationViewMode(const AppContext& context)
{
    const RuntimeState& runtime = context.runtime();
    const PersistedState& persisted = context.persisted();
    if (runtime.selectedCategory >= 0 && runtime.selectedCategory < static_cast<int>(persisted.categories.size())) {
        const Category& category = persisted.categories[static_cast<size_t>(runtime.selectedCategory)];
        if (!category.useGlobalLayout) {
            return category.viewMode;
        }
    }
    return persisted.settings.viewMode;
}

int currentNavigationIconSize(const AppContext& context)
{
    const RuntimeState& runtime = context.runtime();
    const PersistedState& persisted = context.persisted();
    if (runtime.selectedCategory >= 0 && runtime.selectedCategory < static_cast<int>(persisted.categories.size())) {
        const Category& category = persisted.categories[static_cast<size_t>(runtime.selectedCategory)];
        if (!category.useGlobalLayout) {
            return category.iconSize;
        }
    }
    return persisted.settings.iconSize;
}

std::vector<main_dock::NavigationEntry> itemNavigationEntries(const AppContext& context, const std::vector<LaunchItem>& items)
{
    const AppSettings& settings = context.persisted().settings;
    const ItemViewMode viewMode = currentNavigationViewMode(context);
    const float contentWidth = std::max(0.0f, ImGui::GetIO().DisplaySize.x - kRailWidth);
    return main_dock::buildNavigationEntries(items, viewMode, settings.sortMode, currentNavigationIconSize(context),
                                             std::max(0.0f, contentWidth - 44.0f));
}

void switchSelectedCategory(AppContext& context, int delta)
{
    std::vector<Category>& categories = context.persisted().categories;
    if (categories.empty()) {
        return;
    }
    const int count = static_cast<int>(categories.size());
    const int current = std::clamp(context.runtime().selectedCategory, 0, count - 1);
    context.runtime().selectedCategory = (current + delta + count) % count;
    context.runtime().currentFolderStack.clear();
    clearSelection(context);
    gSession.resetMainDockScroll = true;
}

bool moveCurrentItemSelection(AppContext& context, ImGuiKey key)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items || items->empty()) {
        clearSelection(context);
        return false;
    }

    const std::vector<main_dock::NavigationEntry> entries = itemNavigationEntries(context, *items);
    if (entries.empty()) {
        clearSelection(context);
        return false;
    }

    int currentEntry = -1;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if ((*items)[entries[i].itemIndex].id == context.runtime().selectedItemId) {
            currentEntry = i;
            break;
        }
    }
    if (currentEntry < 0) {
        const bool reverse = key == ImGuiKey_UpArrow || key == ImGuiKey_LeftArrow;
        const int target = reverse ? static_cast<int>(entries.size()) - 1 : 0;
        selectSingle(context, (*items)[entries[target].itemIndex]);
        return true;
    }

    main_dock::NavigationDirection direction;
    switch (key) {
    case ImGuiKey_LeftArrow: direction = main_dock::NavigationDirection::Left; break;
    case ImGuiKey_RightArrow: direction = main_dock::NavigationDirection::Right; break;
    case ImGuiKey_UpArrow: direction = main_dock::NavigationDirection::Up; break;
    case ImGuiKey_DownArrow: direction = main_dock::NavigationDirection::Down; break;
    default: return false;
    }
    const int targetEntry =
        main_dock::findNavigationTarget(entries, currentEntry, currentNavigationViewMode(context), direction);

    if (targetEntry < 0 || targetEntry >= static_cast<int>(entries.size()) || targetEntry == currentEntry) {
        return false;
    }
    selectSingle(context, (*items)[entries[targetEntry].itemIndex]);
    return true;
}

void enterCurrentItemListFromCategory(AppContext& context)
{
    if (!context.runtime().selectedItemId.empty()) {
        return;
    }
    std::vector<LaunchItem>* items = currentItems(context);
    if (items && !items->empty()) {
        selectSingle(context, items->front());
    }
}

ItemEditorApi itemEditorApi()
{
    return ItemEditorApi{&editingItems, &selectSingle, &nowUnix};
}

SearchUiState searchUiState()
{
    return SearchUiState{&gSession.focusSearch,       &gSession.searchOpen,     &gSession.searchSubmit,
                         &gSession.searchSelected,    &gSession.searchMove,     &gSession.searchPageMove,
                         &gSession.searchQueryText,   &gSession.searchEditedAt, &gSession.searchCursorEndRequested,
                         &gSession.searchResultsCache};
}

void openNoteById(AppContext& context, const std::string& id)
{
    context.runtime().selectedNoteId = id;
    context.runtime().showNoteQuick = true;
    context.runtime().editSelectedNote = false;
}

void openNoteEditorById(AppContext& context, const std::string& id)
{
    context.runtime().selectedNoteId = id;
    context.runtime().showNotes = true;
    context.runtime().showNoteQuick = false;
    context.runtime().editSelectedNote = true;
}

SearchUiApi searchUiApi()
{
    return SearchUiApi{&findItemById,
                       [](AppContext& context, LaunchItem& item, int showCommand) {
                           runItem(context, item, showCommand);
                       },
                       &drawLaunchIcon,
                       &drawCachedLaunchIcon,
                       &requestLaunchIcon,
                       &openItemEditorWithDraft,
                       &openNoteById,
                       &copyItemToClipboard,
                       &pasteClipboardItem,
                       &clipboardAvailable,
                       &copyTextToClipboard,
                       &itemPropertiesText,
                       &openWithDialog,
                       &openContainingFolder,
                       &showFileProperties};
}

ContentMenuApi contentMenuApi()
{
    return ContentMenuApi{&drawViewMenu,
                          &drawSortMenu,
                          &currentItems,
                          &pasteClipboardItem,
                          &openItemEditor,
                          &appendPlaceholders,
                          &appendItem,
                          &appendNoteItem,
                          &appendBuiltInItem,
                          &runItemsInList,
                          &requestHideMainWindow,
                          []() {
                              clearIconTextureCache();
                          },
                          &requestDeleteIds};
}

ContentMenuState contentMenuState()
{
    return ContentMenuState{clipboardAvailable()};
}

TooltipApi tooltipApi()
{
    return TooltipApi{&timeText};
}

void addScheduledTaskForItem(AppContext& context, const LaunchItem& item)
{
    ScheduledTask task;
    task.id = "task-" + std::to_string(nowUnix()) + "-" + item.id;
    task.name = item.name;
    task.itemId = item.id;
    task.action = item.type == LaunchItemType::VirtualFolder ? ScheduledActionKind::LaunchVirtualFolder : ScheduledActionKind::LaunchItem;
    task.trigger = ScheduledTriggerKind::Daily;
    task.hour = 9;
    task.minute = 0;
    task.onceAt = nowUnix() + 10 * 60;
    context.persisted().scheduledTasks.push_back(std::move(task));
    context.save();
    gSession.showTaskPlanner = true;
}

ItemMenuApi itemMenuApi()
{
    return ItemMenuApi{&isItemSelected,
                       &selectSingle,
                       [](AppContext& context, int itemIndex) {
                           openItemEditor(context, itemIndex);
                       },
                       &requestDeleteSelection,
                       &enterVirtualFolder,
                       &copyItemToClipboard,
                       &copyTextToClipboard,
                       &itemPropertiesText,
                       [](AppContext& context, LaunchItem& item, int showCommand) {
                           runItem(context, item, showCommand);
                       },
                       [](AppContext& context, const LaunchItem& item) {
                           openNoteEditorById(context, item.target.string());
                       },
                       &openWithDialog,
                       &openContainingFolder,
                       &showFileProperties,
                       &rebuildIconCacheForSelection,
                       &addScheduledTaskForItem};
}

ItemViewApi itemViewApi()
{
    return ItemViewApi{&isItemSelected,
                       &handleItemSelectionClick,
                       &selectSingle,
                       &enterVirtualFolder,
                       [](AppContext& context, LaunchItem& item, int showCommand) {
                           runItem(context, item, showCommand);
                       },
                       [](const UiPalette& theme, AppContext& context, std::vector<LaunchItem>& items, int itemIndex) {
                           drawItemMenu(theme, context, items, itemIndex, itemMenuApi());
                       },
                       [](const UiPalette& theme, const AppSettings& settings, const LaunchItem& item, const ImRect& rect) {
                           drawItemTooltip(theme, settings, item, rect, tooltipApi());
                       },
                       &drawLaunchIcon,
                       &drawLaunchIconOnList,
                       &dragPayloadId,
                       &captureDragItemIds,
                       [](const std::string& key, const std::function<void()>& callback) {
                           triggerAfterDragHover(key, callback);
                       },
                       &dragHoverPending,
                       &showFileProperties};
}

CategoryRailApi categoryRailApi()
{
    return CategoryRailApi{&clearSelection,
                           [](AppContext& context, int itemIndex) {
                               openItemEditor(context, itemIndex);
                           },
                           &requestDeleteCategory,
                           &reorderCategory,
                           [](AppContext& context, const std::string& id, std::vector<LaunchItem>& destination) {
                               return moveItemByIdToList(context, id, destination);
                           },
                           [](AppContext& context, const std::vector<std::string>& ids, std::vector<LaunchItem>& destination) {
                               return moveItemIdsToList(context, ids, destination, -1);
                           },
                           [](const std::string& key, const std::function<void()>& callback) {
                               triggerAfterDragHover(key, callback);
                           },
                           &dragHoverPending,
                           &dragPayloadId,
                           &dragItemIds,
                           &runItemsInList,
                           &requestHideMainWindow};
}

CategoryRailState categoryRailState(AppContext& context)
{
    return CategoryRailState{&context.runtime().currentFolderStack, &gSession.openCategoryEditorPopup, &gSession.editingCategoryIndex,
                             &gSession.editingCategoryName,         &gSession.editingCategoryIconName, &gSession.editingCategoryIconColor,
                             &gSession.categoryIconFilter};
}

MainDockGridState mainDockGridState(AppContext& context)
{
    return MainDockGridState{&context.runtime().currentFolderStack, &gSession.resetMainDockScroll};
}

MainDockGridApi mainDockGridApi()
{
    return MainDockGridApi{[](AppContext& context) {
                               return currentItems(context);
                           },
                           &itemsForFolderStack,
                           &clearSelection,
                           &requestHideMainWindow,
                           &itemIndexById,
                           &moveItemByIdToList,
                           &moveItemIdsToList,
                           [](AppContext& context, const std::string& id) {
                               return findItemById(context, id);
                           },
                           &dragPayloadId,
                           &dragItemIds,
                           [](const std::string& key, const std::function<void()>& callback) {
                               triggerAfterDragHover(key, callback);
                           },
                           &dragHoverPending,
                           [](const UiPalette& theme, AppContext& context, const ContentMenuState& state, const ContentMenuApi& api) {
                               drawContentMenu(theme, context, state, api);
                           },
                           contentMenuApi(),
                           &contentMenuState,
                           itemViewApi()};
}

MainDockStateApi mainDockStateApi()
{
    return MainDockStateApi{[](AppContext& context, const std::string& id) {
                                return findItemById(context, id);
                            },
                            [](const AppContext& context, const std::string& id) {
                                return findItemById(context, id);
                            },
                            [](AppContext& context) {
                                return currentItems(context);
                            },
                            &removeItemById,
                            &clearIconCacheForItem,
                            &nowUnix};
}

void handleMainShortcuts(AppContext& context)
{
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (!selectedItemIds(context).empty()) {
            requestDeleteSelection(context);
            return;
        }
        if (!gSession.searchOpen && context.runtime().selectedCategory >= 0 &&
            context.runtime().selectedCategory < static_cast<int>(context.persisted().categories.size()) &&
            context.persisted().categories.size() > 1) {
            requestDeleteCategory(context, context.runtime().selectedCategory);
            return;
        }
    }
    if (io.WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        return;
    }
    if (gSession.searchOpen) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            resetSearchState(context);
        } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
            gSession.searchPageMove = -1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
            gSession.searchPageMove = 1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
            gSession.searchMove = 1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
            gSession.searchMove = -1;
        }
        return;
    }

    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
        switchSelectedCategory(context, -1);
    } else if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
        switchSelectedCategory(context, 1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
        if (context.runtime().selectedItemId.empty()) {
            switchSelectedCategory(context, 1);
        } else {
            moveCurrentItemSelection(context, ImGuiKey_DownArrow);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
        if (context.runtime().selectedItemId.empty()) {
            switchSelectedCategory(context, -1);
        } else {
            moveCurrentItemSelection(context, ImGuiKey_UpArrow);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
        if (context.runtime().selectedItemId.empty() || !moveCurrentItemSelection(context, ImGuiKey_LeftArrow)) {
            clearSelection(context);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
        if (context.runtime().selectedItemId.empty()) {
            enterCurrentItemListFromCategory(context);
        } else {
            moveCurrentItemSelection(context, ImGuiKey_RightArrow);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        return;
    } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        if (LaunchItem* item = findItemById(context, context.runtime().selectedItemId)) {
            runItem(context, *item);
        }
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        selectAllCurrentCategory(context);
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        if (const LaunchItem* item = findItemById(context, context.runtime().selectedItemId)) {
            copyItemToClipboard(*item, false);
        }
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
        if (const LaunchItem* item = findItemById(context, context.runtime().selectedItemId)) {
            copyItemToClipboard(*item, true);
        }
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        pasteClipboardItem(context);
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        openItemEditor(context, -1, LaunchItemType::App);
    } else if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        // F2 always opens the item properties editor.
        if (const LaunchItem* item = findItemById(context, context.runtime().selectedItemId)) {
            if (std::vector<LaunchItem>* items = currentItems(context)) {
                for (int i = 0; i < static_cast<int>(items->size()); ++i) {
                    if ((*items)[i].id == item->id) {
                        openItemEditor(context, i);
                        break;
                    }
                }
            }
        }
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
        // Ctrl+E opens note content editor for note items.
        if (const LaunchItem* item = findItemById(context, context.runtime().selectedItemId)) {
            if (item->type == LaunchItemType::Note) {
                openNoteEditorById(context, item->target.string());
            }
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        clearSelection(context);
    }
}

void startMainWindowDrag(AppContext& context)
{
    const bool lockPosition = context.persisted().settings.lockWindowPosition;
    ImGui::CloseCurrentPopup();
    gSession.draggingMainWindow = false;
    if (lockPosition || !mouseOverMainWindow()) {
        return;
    }
    if (HWND hwnd = mainWindowHandle()) {
        RECT rect{};
        if (GetWindowRect(hwnd, &rect)) {
            gSession.draggingMainWindow = true;
            gSession.mainDragStartMouse = ImGui::GetIO().MousePos;
            gSession.mainDragStartRect = rect;
        }
    }
}

void updateMainWindowDrag(AppContext& context)
{
    const bool lockPosition = context.persisted().settings.lockWindowPosition;
    if (!gSession.draggingMainWindow) {
        return;
    }
    if (lockPosition || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        gSession.draggingMainWindow = false;
        return;
    }

    HWND hwnd = mainWindowHandle();
    if (!hwnd) {
        gSession.draggingMainWindow = false;
        return;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const int x = gSession.mainDragStartRect.left + static_cast<int>(std::lround(mouse.x - gSession.mainDragStartMouse.x));
    const int y = gSession.mainDragStartRect.top + static_cast<int>(std::lround(mouse.y - gSession.mainDragStartMouse.y));
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void updateMainWindowTitleDrag(AppContext& context)
{
    if (ImGui::IsItemActivated()) {
        startMainWindowDrag(context);
    }
}

void handleBlankAreaWindowDrag(AppContext& context, const ImVec2& origin, const ImVec2& size)
{
    const AppSettings& settings = context.persisted().settings;
    if (!settings.dragBlankAreaMoveWindow || settings.lockWindowPosition) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        return;
    }
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsAnyItemHovered() || !mouseOverMainWindow() ||
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        return;
    }
    const ImVec2 mouse = io.MousePos;
    const ImRect body(ImVec2(origin.x, origin.y + kTitleHeight), ImVec2(origin.x + size.x, origin.y + size.y));
    if (!body.Contains(mouse)) {
        return;
    }
    startMainWindowDrag(context);
}
void drawTitleBar(AppContext& context, const ImVec2& origin, const ImVec2& size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool hasBackgroundImage = hasThemeBackground(context.themes.active());
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + kTitleHeight),
                      hasBackgroundImage ? ImGui::ColorConvertFloat4ToU32(withAlpha(gSession.theme.titleBar, 0.82f))
                                         : gSession.theme.titleBar,
                      gSession.theme.windowRounding, ImDrawFlags_RoundCornersTop);
    dl->AddText(ImVec2(origin.x + 16.0f, origin.y + 13.0f), gSession.theme.text, "Launcher");
    float buttonX = origin.x + size.x;
    const auto nextButtonPos = [&]() {
        buttonX -= 58.0f;
        return ImVec2(buttonX, origin.y);
    };
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y));
    const float visibleButtonWidth = (context.persisted().settings.showCloseButton ? 58.0f : 0.0f) +
                                     (context.persisted().settings.showMenuButton ? 58.0f : 0.0f) +
                                     (context.persisted().settings.showSearchButton ? 58.0f : 0.0f);
    ImGui::InvisibleButton("title-drag-close-popups", ImVec2(std::max(0.0f, size.x - visibleButtonWidth), kTitleHeight));
    updateMainWindowTitleDrag(context);

    if (context.persisted().settings.showCloseButton &&
        drawTitleButton(gSession.theme, kTitleHeight, "close-button", nextButtonPos(), Icons::Close)) {
        if (auto* hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw)) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
    }
    if (context.persisted().settings.showMenuButton &&
        drawTitleButton(gSession.theme, kTitleHeight, "menu-button", nextButtonPos(), Icons::Menu)) {
        ImGui::OpenPopup("top-menu");
    }
    if (context.persisted().settings.showSearchButton &&
        drawTitleButton(gSession.theme, kTitleHeight, "search-button", nextButtonPos(), Icons::Search, gSession.searchOpen)) {
        gSession.searchOpen = !gSession.searchOpen;
        if (gSession.searchOpen) {
            gSession.focusSearch = true;
        } else {
            resetSearchState(context);
        }
    }

    LightPopupStyle popupStyle(gSession.theme);
    const bool animatedPopup = ui_anim::pushPopupAppear("top-menu");
    if (ImGui::BeginPopup("top-menu")) {
        suppressCurrentViewportNativeBorder();
        if (beginIconMenu(gSession.theme, Icons::Tools, tr("Tools"))) {
            if (beginIconMenu(gSession.theme, Icons::DataManagement, tr("Data Management"))) {
                if (menuItem(gSession.theme, "", tr("Import Config"))) importConfig(context);
                if (menuItem(gSession.theme, "", tr("Export Config"))) exportConfig(context);
                endIconMenu();
            }
            if (menuItem(gSession.theme, Icons::Folder, tr("Open Location"))) openAppFolder();
            if (menuItem(gSession.theme, Icons::Theme, tr("Theme Editor"))) context.runtime().showThemeEditor = true;
            if (menuItem(gSession.theme, Icons::Task, tr("Task Manager"))) openSystemTool(L"taskmgr.exe");
            endIconMenu();
        }
        if (beginIconMenu(gSession.theme, Icons::QuickSettings, tr("Quick Settings"))) {
            if (menuToggleItem(gSession.theme, Icons::DoNotDisturb, tr("Do Not Disturb"),
                               context.persisted().settings.fullscreenDoNotDisturb)) {
                context.persisted().settings.fullscreenDoNotDisturb = !context.persisted().settings.fullscreenDoNotDisturb;
                context.save();
            }
            if (menuToggleItem(gSession.theme, Icons::Pin, tr("Keep Shown"), context.persisted().settings.alwaysOnTop)) {
                context.persisted().settings.alwaysOnTop = !context.persisted().settings.alwaysOnTop;
                context.save();
            }
            ImGui::Separator();
            if (menuToggleItem(gSession.theme, Icons::TopMost, tr("Always on Top"), context.persisted().settings.alwaysOnTop)) {
                context.persisted().settings.alwaysOnTop = !context.persisted().settings.alwaysOnTop;
                context.save();
            }
            if (menuItem(gSession.theme, Icons::CenterWindow, tr("Reset Position"))) centerMainWindow();
            if (menuToggleItem(gSession.theme, Icons::Pin, tr("Lock Position"), context.persisted().settings.lockWindowPosition)) {
                context.persisted().settings.lockWindowPosition = !context.persisted().settings.lockWindowPosition;
                context.save();
            }
            if (menuToggleItem(gSession.theme, Icons::Pin, tr("Lock Size"), context.persisted().settings.lockWindowSize)) {
                context.persisted().settings.lockWindowSize = !context.persisted().settings.lockWindowSize;
                context.save();
            }
            if (menuToggleItem(gSession.theme, Icons::Layout, tr("Lock Layout"), context.persisted().settings.lockItemLayout)) {
                context.persisted().settings.lockItemLayout = !context.persisted().settings.lockItemLayout;
                context.save();
            }
            endIconMenu();
        }
        if (menuItem(gSession.theme, Icons::Task, tr("Task Planner"))) gSession.showTaskPlanner = true;
        if (menuItem(gSession.theme, Icons::Note, tr("Notes"))) {
            context.runtime().showNotes = true;
            context.runtime().showNoteQuick = false;
            context.runtime().editSelectedNote = false;
        }
        if (menuItem(gSession.theme, Icons::Plugin, tr("Plugins"))) requestSettingsTab(context, 4);
        ImGui::Separator();
        if (menuItem(gSession.theme, Icons::Refresh, tr("Check Updates"))) {
            gSession.showUpdateDialog = true;
            context.updates.checkForUpdates();
        }
        if (menuItem(gSession.theme, Icons::Help, tr("Help"))) context.runtime().showUserGuide = true;
        if (menuItem(gSession.theme, Icons::Settings, tr("Settings"))) {
            context.runtime().showSettings = false;
            gSession.openSettingsNextFrame = true;
        }
        if (menuItem(gSession.theme, Icons::Close, tr("Exit"))) {
            if (auto* hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw)) {
                PostMessageW(hwnd, WM_COMMAND, 3002, 0);
            }
        }
        ImGui::EndPopup();
    }
    if (animatedPopup) {
        ui_anim::popAppearAlpha();
    }
}

bool drawChoiceParamCombo(const UiPalette& theme, const char* id, const InteractiveParam& param, std::string& value)
{
    if (param.choices.empty()) {
        return ImGui::InputTextWithHint(id, tr("Enter value"), &value);
    }

    if (value.empty()) {
        value = param.choices.front();
    }
    bool changed = false;
    const bool comboOpen = beginStyledCombo(theme, id, value.c_str());
    if (comboOpen) {
        ui_anim::pushAppearAlpha(ImGui::GetID(id), 0.10f, 0.20f);
        for (const std::string& choice : param.choices) {
            if (styledComboItem(theme, choice.c_str(), choice == value)) {
                value = choice;
                changed = true;
            }
        }
        ui_anim::popAppearAlpha();
    }
    endStyledCombo(comboOpen);
    return changed;
}

bool drawInteractiveHistorySuggestions(AppContext& context, const UiPalette& theme, int paramIndex, std::string& value, bool inputActive)
{
    if (paramIndex < 0 || paramIndex >= static_cast<int>(gSession.interactiveRunItem.interactiveParams.size())) {
        return false;
    }

    const InteractiveParam& param = gSession.interactiveRunItem.interactiveParams[static_cast<size_t>(paramIndex)];
    const std::string key = interactiveParamKey(gSession.interactiveRunItemId, param, paramIndex);
    bool changed = false;
    const bool keyboardActive = inputActive || gSession.interactiveHistoryParamKey == key;
    if (keyboardActive) {
        if (gSession.interactiveHistoryParamKey != key) {
            gSession.interactiveHistoryParamKey = key;
            gSession.interactiveHistorySelected = -1;
        }
    } else if (gSession.interactiveHistoryParamKey != key) {
        return false;
    }

    std::vector<HistoryCandidate> candidates = interactiveHistoryCandidates(param, value);
    if (candidates.empty()) {
        return false;
    }
    if (gSession.interactiveHistorySelected >= static_cast<int>(candidates.size())) {
        gSession.interactiveHistorySelected = static_cast<int>(candidates.size()) - 1;
    }
    if (gSession.interactiveHistorySelected < -1) {
        gSession.interactiveHistorySelected = -1;
    }

    if (inputActive) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
            gSession.interactiveHistorySelected = gSession.interactiveHistorySelected < 0
                                                      ? 0
                                                      : (gSession.interactiveHistorySelected + 1) % static_cast<int>(candidates.size());
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
            gSession.interactiveHistorySelected =
                gSession.interactiveHistorySelected < 0
                    ? static_cast<int>(candidates.size()) - 1
                    : (gSession.interactiveHistorySelected + static_cast<int>(candidates.size()) - 1) % static_cast<int>(candidates.size());
        } else if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
            gSession.interactiveHistorySelected = gSession.interactiveHistorySelected < 0
                                                      ? 0
                                                      : (gSession.interactiveHistorySelected + 1) % static_cast<int>(candidates.size());
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            gSession.interactiveHistorySelected = gSession.interactiveHistorySelected < 0 ? 0 : gSession.interactiveHistorySelected;
            value = candidates[static_cast<size_t>(gSession.interactiveHistorySelected)].value;
            gSession.interactiveHistoryParamKey.clear();
            gSession.interactiveHistorySelected = -1;
            ImGui::ClearActiveID();
            return true;
        }
    }

    const float rowHeight = ImGui::GetTextLineHeightWithSpacing() + 6.0f;
    const ImVec2 historyPadding(6.0f, 4.0f);
    const float height = std::min(4, static_cast<int>(candidates.size())) * rowHeight + historyPadding.y * 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, historyPadding);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.popupBg);
    ImGui::BeginChild("history", ImVec2(-1.0f, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        const HistoryCandidate& candidate = candidates[static_cast<size_t>(i)];
        ImGui::PushID(i);
        const float buttonWidth = 30.0f;
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const float pickWidth = std::max(40.0f, rowWidth - buttonWidth - 4.0f);
        const ImVec2 pickMax(rowPos.x + pickWidth, rowPos.y + rowHeight);
        const ImVec2 removePos(rowPos.x + rowWidth - buttonWidth, rowPos.y);
        const ImVec2 removeMax(removePos.x + buttonWidth, removePos.y + rowHeight);
        const bool selected = gSession.interactiveHistorySelected >= 0 && i == gSession.interactiveHistorySelected;
        const std::string label = candidate.value + "  (" + std::to_string(std::max(1, candidate.useCount)) + ")";
        if (selected) {
            dl->AddRectFilled(rowPos, pickMax, ImGui::GetColorU32(theme.headerActive), 4.0f);
        } else if (ImGui::IsMouseHoveringRect(rowPos, pickMax)) {
            dl->AddRectFilled(rowPos, pickMax, ImGui::GetColorU32(theme.headerHovered), 4.0f);
        }
        if (ImGui::IsMouseHoveringRect(removePos, removeMax)) {
            dl->AddRectFilled(removePos, removeMax, ImGui::GetColorU32(theme.headerHovered), 4.0f);
        }
        ImGui::InvisibleButton("pick", ImVec2(pickWidth, rowHeight));
        if (ImGui::IsItemClicked()) {
            value = candidate.value;
            gSession.interactiveHistorySelected = i;
            changed = true;
        }
        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        dl->AddText(ImVec2(rowPos.x + 8.0f, rowPos.y + (rowHeight - textSize.y) * 0.5f), theme.text, label.c_str());
        ImGui::SetCursorScreenPos(removePos);
        ImGui::InvisibleButton("remove", ImVec2(buttonWidth, rowHeight));
        const ImVec2 removeMin = ImGui::GetItemRectMin();
        const ImVec2 closeSize = ImGui::CalcTextSize(Icons::Close);
        dl->AddText(ImVec2(removeMin.x + (buttonWidth - closeSize.x) * 0.5f, removeMin.y + (rowHeight - closeSize.y) * 0.5f),
                    theme.textMuted, Icons::Close);
        if (ImGui::IsItemClicked()) {
            const std::string removed = candidate.value;
            removeInteractiveHistoryValue(gSession.interactiveRunItem, paramIndex, removed);
            if (LaunchItem* liveItem = findItemById(context, gSession.interactiveRunItemId)) {
                removeInteractiveHistoryValue(*liveItem, paramIndex, removed);
                context.save();
            }
            const int remaining = static_cast<int>(candidates.size()) - 1;
            if (remaining <= 0) {
                gSession.interactiveHistorySelected = -1;
            } else if (gSession.interactiveHistorySelected >= remaining) {
                gSession.interactiveHistorySelected = remaining - 1;
            }
            changed = true;
        }
        ImGui::SetCursorScreenPos(ImVec2(rowPos.x, rowPos.y + rowHeight));
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    return changed;
}

void drawInteractiveRunDialog(AppContext& context, const UiPalette& theme)
{
    if (!gSession.showInteractiveRun) {
        return;
    }

    setupManagedWindow("LauncherInteractiveRun");
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440.0f, 420.0f), ImGuiCond_Appearing);
    if (gSession.openInteractiveRunPopup) {
        ImGui::SetNextWindowFocus();
        gSession.openInteractiveRunPopup = false;
    }

    ManagedWindowStyle windowStyle(theme);
    bool open = true;
    if (ImGui::Begin(tr("Run###interactive-run"), &open,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        applyManagedViewportChrome(ImGui::GetWindowViewport()->PlatformHandleRaw, context.themes.active(), theme);
        drawManagedTitleBar(theme, tr("Run"), open);
        if (!open) {
            gSession.showInteractiveRun = false;
            gSession.interactiveHistoryParamKey.clear();
            gSession.interactiveHistorySelected = -1;
            ImGui::End();
            return;
        }

        ImGui::SetCursorPos(ImVec2(14.0f, kUiTitleHeight + 12.0f));
        ImGui::BeginChild("interactive-run-content", ImVec2(-14.0f, -58.0f), ImGuiChildFlags_None);
        ImGui::TextUnformatted(gSession.interactiveRunItem.name.c_str());
        if (!gSession.interactiveRunItem.remark.empty()) {
            ImGui::TextDisabled("%s", gSession.interactiveRunItem.remark.c_str());
        }
        ImGui::Dummy(ImVec2(1.0f, 8.0f));

        const std::vector<InteractiveParam>& params = gSession.interactiveRunItem.interactiveParams;
        while (gSession.interactiveRunValues.size() < params.size()) {
            gSession.interactiveRunValues.push_back(defaultParamValue(params[gSession.interactiveRunValues.size()]));
        }

        for (int i = 0; i < static_cast<int>(params.size()); ++i) {
            const InteractiveParam& param = params[static_cast<size_t>(i)];
            std::string& value = gSession.interactiveRunValues[static_cast<size_t>(i)];
            const std::string label = param.label.empty() ? effectiveParamId(param, i) : param.label;
            ImGui::TextUnformatted(label.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::PushID(i);
            if (param.kind == InteractiveParamKind::Number) {
                double number = value.empty() ? param.minValue : std::strtod(value.c_str(), nullptr);
                if (param.maxValue >= param.minValue) {
                    number = std::clamp(number, param.minValue, param.maxValue);
                }
                const double step = param.step <= 0.0 ? 1.0 : param.step;
                if (ImGui::InputDouble("##value", &number, step, step * 10.0, "%.6g")) {
                    if (param.maxValue >= param.minValue) {
                        number = std::clamp(number, param.minValue, param.maxValue);
                    }
                    char buffer[64]{};
                    std::snprintf(buffer, sizeof(buffer), "%.6g", number);
                    value = buffer;
                }
                const bool inputActive = ImGui::IsItemActive() || ImGui::IsItemFocused();
                drawInteractiveHistorySuggestions(context, theme, i, value, inputActive);
            } else if (param.kind == InteractiveParamKind::Choice) {
                drawChoiceParamCombo(theme, "##value", param, value);
            } else {
                ImGui::InputTextWithHint("##value", tr("Enter value"), &value);
                const bool inputActive = ImGui::IsItemActive() || ImGui::IsItemFocused();
                drawInteractiveHistorySuggestions(context, theme, i, value, inputActive);
            }
            ImGui::PopID();
            ImGui::Dummy(ImVec2(1.0f, 6.0f));
        }
        ImGui::EndChild();

        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 194.0f);
        if (ImGui::Button(tr("Run"), ImVec2(86.0f, 32.0f))) {
            LaunchItem launchItem = withInteractiveValues(gSession.interactiveRunItem, gSession.interactiveRunValues);
            if (!gSession.interactiveRunSearchText.empty()) {
                launchItem = withSearchVariables(launchItem, gSession.interactiveRunSearchText);
            }
            bool launched = false;
            if (LaunchItem* liveItem = findItemById(context, gSession.interactiveRunItemId)) {
                launched = launchResolvedItem(context, *liveItem, std::move(launchItem), gSession.interactiveRunShowCommand);
                if (launched) {
                    launch_params::recordInteractiveHistory(*liveItem, gSession.interactiveRunValues, nowUnix());
                    context.save();
                }
            } else {
                LaunchItem fallback = gSession.interactiveRunItem;
                launched = launchResolvedItem(context, fallback, std::move(launchItem), gSession.interactiveRunShowCommand);
            }
            if (launched) {
                gSession.showInteractiveRun = false;
                gSession.interactiveHistoryParamKey.clear();
                gSession.interactiveHistorySelected = -1;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"), ImVec2(86.0f, 32.0f))) {
            gSession.showInteractiveRun = false;
            gSession.interactiveHistoryParamKey.clear();
            gSession.interactiveHistorySelected = -1;
        }
    }
    ImGui::End();
}

void enterVirtualFolder(AppContext& context, const LaunchItem& item)
{
    if (item.type != LaunchItemType::VirtualFolder) {
        return;
    }
    context.runtime().currentFolderStack.push_back(item.id);
    clearSelection(context);
}

} // namespace

void closeMainDockSearch(AppContext& context)
{
    resetSearchState(context);
}

void openMainDockSearch(AppContext& context)
{
    gSession.searchOpen = true;
    gSession.focusSearch = true;
    gSession.searchSubmit = false;
    gSession.searchSelected = 0;
    gSession.searchMove = 0;
    gSession.searchPageMove = 0;
    gSession.searchEditedAt = ImGui::GetTime();
    gSession.searchQueryText = context.runtime().searchText;
}

void openMainDockSearchWithText(AppContext& context, std::string text)
{
    context.runtime().searchText = std::move(text);
    openMainDockSearch(context);
    gSession.searchSubmit = false;
    gSession.searchQueryText = context.runtime().searchText;
    gSession.searchEditedAt = ImGui::GetTime();
    gSession.searchCursorEndRequested = true;
}

void setMainDockDevice(ID3D11Device* device)
{
    gResources.setDevice(device);
}

void releaseMainDockCaches()
{
    gResources.clear();
    resetIconLoadScheduling();
}

void releaseMainDockBackgroundCache()
{
    gResources.clearBackground();
}

void closeMainDockWindows(AppContext& context)
{
    context.runtime().showSettings = false;
    context.runtime().showThemeEditor = false;
    context.runtime().showNotes = false;
    context.runtime().showNoteQuick = false;
    context.runtime().editSelectedNote = false;
    if (context.persisted().settings.hideSearchAfterMainClose) {
        resetSearchState(context);
    }
    gSession.showTaskPlanner = false;
    gSession.openSettingsNextFrame = false;
    gSession.showItemEditor = false;
    gSession.openItemEditorPopup = false;
    gSession.editingCategory = -1;
    gSession.editingItem = -1;
    gSession.editingFolderId.clear();
    gSession.editingDraft = {};
    gSession.editingTarget.clear();
    gSession.editingStartDir.clear();
    gSession.editingRemark.clear();
    gSession.editingIcon.clear();
    gSession.showInteractiveRun = false;
    gSession.openInteractiveRunPopup = false;
    gSession.interactiveRunItem = {};
    gSession.interactiveRunItemId.clear();
    gSession.interactiveRunSearchText.clear();
    gSession.interactiveRunValues.clear();
    gSession.interactiveHistoryParamKey.clear();
    gSession.interactiveHistorySelected = -1;
    gSession.showBuildInfo = false;
    gSession.openCategoryEditorPopup = false;
    gSession.editingCategoryIndex = -1;
    gSession.editingCategoryName.clear();
    gSession.editingCategoryIconName.clear();
    gSession.editingCategoryIconColor.clear();
    gSession.categoryIconFilter.clear();
    clearDeleteState();
    clearClipboardState();
    clearDragHoverState();
    clearDragItemIdsSnapshot();
    clearMainDockDragVisualState();
    if (context.persisted().settings.clearSelectionAfterMainClose) {
        clearSelection(context);
    }
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGuiIO& io = ImGui::GetIO();
        io.ClearInputKeys();
        io.ClearInputMouse();
        ImGui::ClearActiveID();
        ImGui::ClearDragDrop();
        ImGui::ClosePopupsExceptModals();
    }
}

void resetMainDockScrollOnNextFrame()
{
    gSession.resetMainDockScroll = true;
}

void drawMainDock(AppContext& context)
{
    configureMainDockState(mainDockStateApi());
    resolvePendingItemSelectionClick(context);
    setLocale(context.persisted().settings.language);
    setMenuShortcutHintsVisible(context.persisted().settings.showMenuShortcutHints);
    applyUiStyle(context.themes.active());
    gSession.theme = uiPalette(context.themes.active());
    const UpdateSnapshot updateSnapshot = context.updates.snapshot();
    if (updateSnapshot.state == UpdateState::Checking && updateSnapshot.automaticCheck) {
        gSession.automaticUpdatePromptVersion.clear();
    } else if (updateSnapshot.state == UpdateState::Available && updateSnapshot.automaticCheck && !updateSnapshot.latestVersion.empty() &&
               gSession.automaticUpdatePromptVersion != updateSnapshot.latestVersion) {
        gSession.automaticUpdatePromptVersion = updateSnapshot.latestVersion;
        gSession.showUpdateDialog = true;
    }
    if (gSession.useDefaultIcons != context.persisted().settings.useDefaultIcons) {
        gSession.useDefaultIcons = context.persisted().settings.useDefaultIcons;
        gResources.clearIcons(true);
        resetIconLoadScheduling();
    }
    if (gSession.openSettingsNextFrame) {
        context.runtime().showSettings = true;
        gSession.openSettingsNextFrame = false;
    }
    if (context.runtime().showThemeEditor && !gSession.themeEditorWasOpen) {
        clearIconTextureCache();
        resetIconLoadScheduling();
    }
    gSession.themeEditorWasOpen = context.runtime().showThemeEditor;

    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size(io.DisplaySize.x, io.DisplaySize.y);
    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::Begin("LauncherRoot", nullptr, flags);
    const bool hasBackgroundImage = hasThemeBackground(context.themes.active());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), gSession.theme.contentBg, gSession.theme.windowRounding);
    if (hasBackgroundImage) {
        drawConfiguredBackground(context, context.themes.active(), origin, size);
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        handleMainShortcuts(context);
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        gSession.searchOpen = true;
        gSession.focusSearch = true;
    }
    drawTitleBar(context, origin, size);
    drawBuildInfoPopup(context.themes.active(), gSession.theme, gSession.showBuildInfo, &openAppFolder);
    drawDeleteConfirmPopup(gSession.theme, context, takeDeleteConfirmState(), &deletePendingItems);

    const ImVec2 bodyOrigin(origin.x, origin.y + kTitleHeight);
    const float bodyHeight = std::max(0.0f, size.y - kTitleHeight);
    if (!gSession.searchOpen) {
        drawCategoryRail(gSession.theme, context, categoryRailState(context), categoryRailApi(), bodyOrigin, bodyHeight, kRailWidth);

        const ImVec2 contentOrigin(origin.x + kRailWidth, bodyOrigin.y);
        const ImVec2 contentSize(std::max(0.0f, size.x - kRailWidth), bodyHeight);
        beginIconLoadFrame(context);
        drawItemGrid(gSession.theme, context, mainDockGridState(context), mainDockGridApi(), contentOrigin, contentSize);
    } else {
        drawSearchBar(gSession.theme, context, searchUiState(), bodyOrigin, size.x, kSearchHeight);
        beginIconLoadFrame(context);
        drawSearchResults(gSession.theme, context, searchUiState(), searchUiApi(), ImVec2(bodyOrigin.x, bodyOrigin.y + kSearchHeight),
                          ImVec2(size.x, std::max(0.0f, bodyHeight - kSearchHeight)));
    }
    processPendingIconRequests();
    handleBlankAreaWindowDrag(context, origin, size);
    updateMainWindowDrag(context);
    if (!context.persisted().settings.lockWindowSize) {
        drawResizeHandles(origin, size, kMinWindowWidth, kMinWindowHeight);
    }
    if (gSession.theme.windowOutlineSize > 0.0f) {
        const float inset = gSession.theme.windowOutlineSize * 0.5f;
        dl->AddRect(ImVec2(origin.x + inset, origin.y + inset), ImVec2(origin.x + size.x - inset, origin.y + size.y - inset),
                    ImGui::ColorConvertFloat4ToU32(gSession.theme.windowOutline), gSession.theme.windowRounding, 0,
                    gSession.theme.windowOutlineSize);
    }
    ImGui::End();
    ImGui::PopStyleColor();

    drawItemEditor(gSession.theme, context,
                   {&gSession.showItemEditor, &gSession.openItemEditorPopup, &gSession.editingCategory, &gSession.editingFolderId,
                    &gSession.editingItem, &gSession.editingDraft, &gSession.editingTarget, &gSession.editingStartDir,
                    &gSession.editingRemark, &gSession.editingIcon},
                   itemEditorApi());
    drawSettingsPanel(context);
    drawThemeEditor(context);
    drawNotesPanel(context, gSession.theme);
    drawTaskPlannerWindow(context, context.themes.active(), gSession.theme, gSession.showTaskPlanner);
    drawUserGuideWindow(context, gSession.theme);
    drawInteractiveRunDialog(context, gSession.theme);
    drawUpdateDialog(context, gSession.theme, gSession.showUpdateDialog);
    snapMainWindowIfNeeded(context.persisted().settings);
}

} // namespace launcher
