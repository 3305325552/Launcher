#pragma once

#include "launcher/Models.hpp"
#include "ui/MainDockDialogs.hpp"

#include <functional>
#include <string>
#include <vector>

namespace launcher {

struct AppContext;

struct MainDockStateApi {
    LaunchItem* (*findItemById)(AppContext&, const std::string&) = nullptr;
    const LaunchItem* (*findItemByIdConst)(const AppContext&, const std::string&) = nullptr;
    std::vector<LaunchItem>* (*currentItems)(AppContext&) = nullptr;
    bool (*removeItemById)(AppContext&, const std::string&) = nullptr;
    void (*clearIconCacheForItem)(const LaunchItem&) = nullptr;
    std::int64_t (*nowUnix)() = nullptr;
};

void configureMainDockState(const MainDockStateApi& api);
bool listLayoutLockedForStack(const AppContext& context, const std::vector<std::string>& stack);
bool currentListLayoutLocked(const AppContext& context);
void setCurrentListLayoutLocked(AppContext& context, bool locked);

void triggerAfterDragHover(const std::string& key, const std::function<void()>& callback);
bool dragHoverPending(const std::string& key);
void clearDragHoverState();

bool isItemSelected(const LaunchItem& item);
void clearSelection(AppContext& context);
void selectIds(AppContext& context, const std::vector<std::string>& ids, const std::string& activeId);
void selectSingle(AppContext& context, const LaunchItem& item);
void toggleSelection(AppContext& context, const LaunchItem& item);
void selectRange(AppContext& context, const std::vector<LaunchItem>& items, int itemIndex);
void handleItemSelectionClick(AppContext& context, const std::vector<LaunchItem>& items, int itemIndex);
void resolvePendingItemSelectionClick(AppContext& context);
std::vector<std::string> selectedItemIds(const AppContext& context);
void captureDragItemIds(const AppContext& context, const std::string& primaryId);
std::vector<std::string> dragItemIds(const AppContext& context, const std::string& primaryId);
void clearDragItemIdsSnapshot();

bool clipboardAvailable();
void clearClipboardState();
void pasteClipboardItem(AppContext& context);
void copyItemToClipboard(const LaunchItem& item, bool cut);
void copyTextToClipboard(const std::string& text);

void clearDeleteState();
bool hasPendingDeleteState();
void requestDeleteIds(const AppContext& context, std::vector<std::string> ids);
void requestDeleteSelection(const AppContext& context);
void requestDeleteCategory(const AppContext& context, int index);
DeleteConfirmState takeDeleteConfirmState();
void deletePendingItems(AppContext& context);

void rebuildIconCacheForSelection(AppContext& context, const LaunchItem& fallback);

} // namespace launcher
