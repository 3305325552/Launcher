#include "ui/dock/MainDockGrid.hpp"
#include "ui/dock/MainDockNotes.hpp"

#include "app/AppContext.hpp"
#include "core/NotesStore.hpp"
#include "ui/common/Localization.hpp"
#include "ui/dock/MainDockDragPayload.hpp"
#include "ui/common/MaterialIcons.hpp"
#include "ui/dock/MainDockItemViews.hpp"
#include "ui/dock/MainDockState.hpp"
#include "ui/dock/MainDockWin32.hpp"
#include "ui/common/UiAnimation.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace launcher {
namespace {

std::unordered_map<std::string, ImVec2> gPendingDropPreviewPositions;
std::vector<std::string> gActiveDraggedIds;
std::vector<std::string> gReleasedDraggedIds;
int gReleasedDraggedFrame = -1;
std::vector<LaunchItem> gActiveDraggedPreviewItems;
std::optional<ImVec2> gActiveDraggedPreviewAnchor;
std::unordered_map<std::string, ImVec2> gActiveDraggedCurrentPreviewPositions;
std::unordered_map<std::string, ImVec2> gLastItemDisplayPositions;
bool gMainWindowFocusedForNoteDrag = false;

std::vector<int> orderedIndices(const std::vector<LaunchItem>& items, SortMode mode)
{
    std::vector<int> indices(items.size());
    for (int i = 0; i < static_cast<int>(indices.size()); ++i) {
        indices[i] = i;
    }
    if (mode == SortMode::Free) {
        return indices;
    }

    std::stable_sort(indices.begin(), indices.end(), [&](int lhs, int rhs) {
        const LaunchItem& a = items[lhs];
        const LaunchItem& b = items[rhs];
        switch (mode) {
        case SortMode::Name: return a.name < b.name;
        case SortMode::Type: return static_cast<int>(a.type) < static_cast<int>(b.type);
        case SortMode::RunCount: return a.runCount > b.runCount;
        case SortMode::CreatedAt: return a.createdAt > b.createdAt;
        case SortMode::LastRunAt: return a.lastRunAt > b.lastRunAt;
        case SortMode::LastEditedAt: return a.lastEditedAt > b.lastEditedAt;
        case SortMode::Free:
        default: return lhs < rhs;
        }
    });
    return indices;
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

ImRect folderDropZone(const ImVec2& pos, float width, float height)
{
    return ImRect(ImVec2(pos.x + width * 0.18f, pos.y + height * 0.14f), ImVec2(pos.x + width * 0.82f, pos.y + height * 0.86f));
}

ImRect folderAutoEnterZone(const ImVec2& pos, float width, float height)
{
    return ImRect(ImVec2(pos.x + width * 0.28f, pos.y + height * 0.24f), ImVec2(pos.x + width * 0.72f, pos.y + height * 0.76f));
}

ImRect folderReorderBlockZone(const ImVec2& pos, float width, float height)
{
    return ImRect(pos, ImVec2(pos.x + width, pos.y + height));
}

ImRect listFolderDropZone(const ImVec2& pos, float width, float height)
{
    ImRect zone = folderDropZone(pos, width, height);
    zone.Min.x = pos.x;
    zone.Max.x = std::max(zone.Max.x, pos.x + std::min(width, 68.0f));
    return zone;
}

ImRect listFolderAutoEnterZone(const ImVec2& pos, float width, float height)
{
    ImRect zone = folderAutoEnterZone(pos, width, height);
    zone.Min.x = pos.x;
    zone.Max.x = std::max(zone.Max.x, pos.x + std::min(width, 68.0f));
    return zone;
}

bool isFolderHoveredForAutoEnter(const LaunchItem& item, const ImVec2& tile, float tileW, float tileH)
{
    if (item.type != LaunchItemType::VirtualFolder) {
        return false;
    }
    return folderAutoEnterZone(tile, tileW, tileH).Contains(ImGui::GetIO().MousePos);
}

int effectiveNameLines(const AppContext& context)
{
    const PersistedState& persisted = context.persisted();
    const RuntimeState& runtime = context.runtime();
    if (runtime.selectedCategory >= 0 && runtime.selectedCategory < static_cast<int>(persisted.categories.size())) {
        const Category& category = persisted.categories[runtime.selectedCategory];
        if (!category.useGlobalLayout) {
            return category.nameLines;
        }
    }
    return persisted.settings.nameLines;
}

bool isListFolderDropHovered(const LaunchItem& item, const ImVec2& row, float width, float height)
{
    if (item.type != LaunchItemType::VirtualFolder) {
        return false;
    }
    return listFolderDropZone(row, width, height).Contains(ImGui::GetIO().MousePos);
}

bool isFolderReorderBlocked(const LaunchItem& item, const ImVec2& pos, float width, float height)
{
    return item.type == LaunchItemType::VirtualFolder && folderReorderBlockZone(pos, width, height).Contains(ImGui::GetIO().MousePos);
}

std::vector<int> orderWithGhostSlot(const std::vector<int>& order, const std::unordered_set<int>& draggedIndices, int insertIndex,
                                    int ghostCount)
{
    std::vector<int> result;
    result.reserve(order.size() + ghostCount);
    int visibleInsert = 0;
    for (int index : order) {
        if (draggedIndices.contains(index)) {
            continue;
        }
        if (visibleInsert == insertIndex) {
            for (int ghost = 0; ghost < ghostCount; ++ghost) {
                result.push_back(-1);
            }
        }
        result.push_back(index);
        ++visibleInsert;
    }
    if (visibleInsert == insertIndex) {
        for (int ghost = 0; ghost < ghostCount; ++ghost) {
            result.push_back(-1);
        }
    }
    return result;
}

float trailingDropPreviewThreshold(float itemExtent)
{
    return std::clamp(itemExtent * 0.55f, 28.0f, 52.0f);
}

float titleRowHeight(const LaunchItem& item);

int ghostInsertIndexForCurrentList(const MainDockGridApi& api, const std::vector<LaunchItem>& items, const std::vector<int>& order,
                                   const std::unordered_set<int>& draggedIndices, float startX, float startY, float availableWidth,
                                   float tileW, float tileH, float gapX, float gapY, int columns, bool draggingTitle)
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->IsDataType(drag_payload::kItemId) || !api.dragPayloadId || !api.itemIndexById) {
        return -1;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float maxX = startX + availableWidth;
    if (mouse.x < startX - 8.0f || mouse.x > maxX + 8.0f || mouse.y < startY - 8.0f) {
        return -1;
    }

    struct InsertRow {
        bool title = false;
        int firstOrdinal = 0;
        int count = 0;
        float y = 0.0f;
        float height = 0.0f;
        std::vector<int> itemIndices;
    };

    std::vector<InsertRow> rows;
    int visibleOrdinal = 0;
    int visualColumn = 0;
    float cursorY = startY;
    float rowY = startY;
    std::vector<int> rowItems;

    const auto flushTileRow = [&]() {
        if (visualColumn <= 0) {
            return;
        }
        rows.push_back(InsertRow{false, visibleOrdinal - visualColumn, visualColumn, rowY, tileH, rowItems});
        visualColumn = 0;
        rowItems.clear();
        cursorY = rowY + tileH + gapY;
    };

    for (int index : order) {
        if (draggedIndices.contains(index)) {
            continue;
        }
        const LaunchItem& item = items[index];
        if (item.type == LaunchItemType::Title) {
            flushTileRow();
            const float height = titleRowHeight(item);
            rows.push_back(InsertRow{true, visibleOrdinal, 1, cursorY, height, {index}});
            cursorY += height + gapY;
            ++visibleOrdinal;
            continue;
        }
        if (visualColumn == 0) {
            rowY = cursorY;
        }
        rowItems.push_back(index);
        ++visualColumn;
        ++visibleOrdinal;
        if (visualColumn >= columns) {
            flushTileRow();
        }
    }
    flushTileRow();

    const int visibleCount = visibleOrdinal;
    if (visibleCount == 0) {
        return 0;
    }
    const InsertRow& lastRow = rows.back();
    const float occupiedBottom = lastRow.y + lastRow.height;
    if (mouse.y > occupiedBottom + trailingDropPreviewThreshold(tileH)) {
        return -1;
    }

    for (const InsertRow& row : rows) {
        if (mouse.y < row.y - gapY * 0.5f) {
            return row.firstOrdinal;
        }
        if (mouse.y > row.y + row.height + gapY * 0.5f) {
            continue;
        }
        if (row.title) {
            const int insertAt = mouse.y >= row.y + row.height * 0.5f ? row.firstOrdinal + 1 : row.firstOrdinal;
            return std::clamp(insertAt, 0, visibleCount);
        }

        if (draggingTitle) {
            const int insertAt = mouse.y >= row.y + row.height * 0.5f ? row.firstOrdinal + row.count : row.firstOrdinal;
            return std::clamp(insertAt, 0, visibleCount);
        }

        if (mouse.x <= startX) {
            return row.firstOrdinal;
        }
        int column = static_cast<int>(std::floor((mouse.x - startX) / std::max(1.0f, tileW + gapX)));
        if (column >= row.count) {
            return std::clamp(row.firstOrdinal + row.count, 0, visibleCount);
        }
        column = std::clamp(column, 0, row.count - 1);
        const ImVec2 tile(startX + column * (tileW + gapX), row.y);
        if (column < static_cast<int>(row.itemIndices.size())) {
            const int itemIndex = row.itemIndices[column];
            if (isFolderReorderBlocked(items[itemIndex], tile, tileW, tileH)) {
                return -1;
            }
        }
        const int insertAt = row.firstOrdinal + column + (mouse.x >= tile.x + tileW * 0.5f ? 1 : 0);
        return std::clamp(insertAt, 0, visibleCount);
    }
    return visibleCount;
}

int ghostInsertIndexForCurrentListRows(const MainDockGridApi& api, const std::vector<LaunchItem>& items, const std::vector<int>& order,
                                       const std::unordered_set<int>& draggedIndices, float startX, float width, float startY,
                                       float rowHeight)
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->IsDataType(drag_payload::kItemId) || !api.dragPayloadId || !api.itemIndexById) {
        return -1;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (mouse.x < startX - 8.0f || mouse.x > startX + width + 8.0f || mouse.y < startY - 8.0f) {
        return -1;
    }
    int visibleCount = 0;
    float occupiedBottom = startY;
    for (int index : order) {
        if (!draggedIndices.contains(index)) {
            ++visibleCount;
            occupiedBottom += items[index].type == LaunchItemType::Title ? titleRowHeight(items[index]) : rowHeight;
        }
    }
    if (visibleCount == 0) {
        return 0;
    }
    if (mouse.y > occupiedBottom + trailingDropPreviewThreshold(rowHeight)) {
        return -1;
    }
    int row = 0;
    float cursorY = startY;
    for (int index : order) {
        if (draggedIndices.contains(index)) {
            continue;
        }
        const float currentHeight = items[index].type == LaunchItemType::Title ? titleRowHeight(items[index]) : rowHeight;
        if (mouse.y < cursorY + currentHeight) {
            if (isFolderReorderBlocked(items[index], ImVec2(startX, cursorY), width, currentHeight)) {
                return -1;
            }
            return std::clamp(row, 0, visibleCount);
        }
        cursorY += currentHeight;
        ++row;
    }
    return std::clamp(row, 0, visibleCount);
}

int hoveredFolderDropIndexForRows(const std::vector<LaunchItem>& items, const std::vector<int>& order,
                                  const std::unordered_set<int>& draggedIndices, float startX, float width, float startY, float rowHeight)
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (mouse.x < startX - 8.0f || mouse.x > startX + width + 8.0f || mouse.y < startY - 8.0f) {
        return -1;
    }

    int visibleCount = 0;
    float occupiedBottom = startY;
    for (int index : order) {
        if (!draggedIndices.contains(index)) {
            ++visibleCount;
            occupiedBottom += items[index].type == LaunchItemType::Title ? titleRowHeight(items[index]) : rowHeight;
        }
    }
    if (visibleCount <= 0 || mouse.y > occupiedBottom + 8.0f) {
        return -1;
    }

    int row = 0;
    float cursorY = startY;
    for (int index : order) {
        if (draggedIndices.contains(index)) {
            continue;
        }
        const float currentHeight = items[index].type == LaunchItemType::Title ? titleRowHeight(items[index]) : rowHeight;
        if (mouse.y < cursorY + currentHeight) {
            return isListFolderDropHovered(items[index], ImVec2(startX, cursorY), width, currentHeight) ? index : -1;
        }
        cursorY += currentHeight;
        ++row;
    }
    return -1;
}

int hoveredFolderDropIndexForGrid(const std::vector<LaunchItem>& items, const std::vector<int>& order,
                                  const std::unordered_set<int>& draggedIndices, float startX, float startY, float tileW, float tileH,
                                  float gapX, float gapY, int columns)
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float maxX = startX + columns * tileW + std::max(0, columns - 1) * gapX;
    if (mouse.x < startX - 8.0f || mouse.x > maxX + 8.0f) {
        return -1;
    }

    int visualColumn = 0;
    float cursorY = startY;
    for (int index : order) {
        if (!draggedIndices.contains(index)) {
            const LaunchItem& item = items[index];
            if (item.type == LaunchItemType::Title) {
                if (visualColumn != 0) {
                    visualColumn = 0;
                    cursorY += tileH + gapY;
                }
                cursorY += titleRowHeight(item) + gapY;
                continue;
            }

            const ImVec2 tile(startX + visualColumn * (tileW + gapX), cursorY);
            if (item.type == LaunchItemType::VirtualFolder && folderDropZone(tile, tileW, tileH).Contains(mouse)) {
                return index;
            }
            ++visualColumn;
            if (visualColumn >= columns) {
                visualColumn = 0;
                cursorY += tileH + gapY;
            }
        }
    }
    return -1;
}

const LaunchItem* draggedPayloadItem(const MainDockGridApi& api, AppContext& context)
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->IsDataType(drag_payload::kItemId) || !api.dragPayloadId || !api.findItemById) {
        return nullptr;
    }
    const std::string id = api.dragPayloadId(payload);
    return id.empty() ? nullptr : api.findItemById(context, id);
}

std::vector<std::string> draggedPayloadItemIds(const MainDockGridApi& api, const AppContext& context)
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->IsDataType(drag_payload::kItemId) || !api.dragPayloadId) {
        return {};
    }
    const std::string primaryId = api.dragPayloadId(payload);
    if (primaryId.empty()) {
        return {};
    }
    if (api.dragItemIds) {
        return api.dragItemIds(context, primaryId);
    }
    return {primaryId};
}

std::vector<std::string> activeOrReleasedDragIds(const MainDockGridApi& api, const AppContext& context)
{
    std::vector<std::string> dragIds = draggedPayloadItemIds(api, context);
    if (!dragIds.empty()) {
        return dragIds;
    }
    return gReleasedDraggedFrame == ImGui::GetFrameCount() ? gReleasedDraggedIds : std::vector<std::string>{};
}

std::vector<int> draggedIndicesInDisplayOrder(const std::vector<LaunchItem>& items, const std::vector<std::string>& draggedIds)
{
    if (draggedIds.empty()) {
        return {};
    }
    std::unordered_set<std::string> draggedSet(draggedIds.begin(), draggedIds.end());
    std::vector<int> indices;
    indices.reserve(draggedIds.size());
    for (int index = 0; index < static_cast<int>(items.size()); ++index) {
        if (draggedSet.contains(items[index].id)) {
            indices.push_back(index);
        }
    }
    return indices;
}

std::unordered_set<int> draggedIndexSet(const std::vector<int>& draggedIndices)
{
    return std::unordered_set<int>(draggedIndices.begin(), draggedIndices.end());
}

std::vector<const LaunchItem*> draggedItemsInDisplayOrder(const std::vector<LaunchItem>& items, const std::vector<int>& draggedIndices)
{
    std::vector<const LaunchItem*> draggedItems;
    draggedItems.reserve(draggedIndices.size());
    for (int index : draggedIndices) {
        if (index >= 0 && index < static_cast<int>(items.size())) {
            draggedItems.push_back(&items[index]);
        }
    }
    return draggedItems;
}

void storePendingDropPreviewPositionsFromActiveSession();

void syncActiveDraggedPreviewItems(const MainDockGridApi& api, AppContext& context)
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->IsDataType(drag_payload::kItemId)) {
        if (gReleasedDraggedFrame != ImGui::GetFrameCount()) {
            gReleasedDraggedIds.clear();
            gReleasedDraggedFrame = -1;
        }
        if (!gActiveDraggedPreviewItems.empty()) {
            storePendingDropPreviewPositionsFromActiveSession();
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                gReleasedDraggedIds = gActiveDraggedIds;
                gReleasedDraggedFrame = ImGui::GetFrameCount();
            }
        }
        gActiveDraggedIds.clear();
        gActiveDraggedPreviewItems.clear();
        gActiveDraggedPreviewAnchor.reset();
        gActiveDraggedCurrentPreviewPositions.clear();
        return;
    }
    const std::vector<std::string> dragIds = draggedPayloadItemIds(api, context);
    if (dragIds.empty()) {
        if (gReleasedDraggedFrame != ImGui::GetFrameCount()) {
            gReleasedDraggedIds.clear();
            gReleasedDraggedFrame = -1;
        }
        if (!gActiveDraggedPreviewItems.empty()) {
            storePendingDropPreviewPositionsFromActiveSession();
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                gReleasedDraggedIds = gActiveDraggedIds;
                gReleasedDraggedFrame = ImGui::GetFrameCount();
            }
        }
        gActiveDraggedIds.clear();
        gActiveDraggedPreviewItems.clear();
        gActiveDraggedPreviewAnchor.reset();
        gActiveDraggedCurrentPreviewPositions.clear();
        return;
    }
    gReleasedDraggedIds.clear();
    gReleasedDraggedFrame = -1;
    if (gActiveDraggedIds == dragIds && gActiveDraggedPreviewItems.size() == dragIds.size()) {
        return;
    }
    gActiveDraggedIds = dragIds;
    gActiveDraggedPreviewItems.clear();
    gActiveDraggedPreviewAnchor.reset();
    gActiveDraggedCurrentPreviewPositions.clear();
    gPendingDropPreviewPositions.clear();
    gActiveDraggedPreviewItems.reserve(dragIds.size());
    if (!api.findItemById) {
        return;
    }
    const ImVec2 fallbackStart(ImGui::GetIO().MousePos.x + 14.0f, ImGui::GetIO().MousePos.y + 14.0f);
    for (const std::string& id : dragIds) {
        if (const LaunchItem* item = api.findItemById(context, id)) {
            gActiveDraggedPreviewItems.push_back(*item);
            const auto it = gLastItemDisplayPositions.find(id);
            const ImVec2 start = it != gLastItemDisplayPositions.end() ? it->second : fallbackStart;
            if (!gActiveDraggedPreviewAnchor.has_value()) {
                gActiveDraggedPreviewAnchor = start;
            }
            gActiveDraggedCurrentPreviewPositions[id] = start;
        }
    }
}

std::vector<const LaunchItem*> activeDraggedPreviewItems()
{
    std::vector<const LaunchItem*> items;
    items.reserve(gActiveDraggedPreviewItems.size());
    for (const LaunchItem& item : gActiveDraggedPreviewItems) {
        items.push_back(&item);
    }
    return items;
}

bool noteDragPayloadActive()
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    return payload && payload->IsDataType(drag_payload::kNoteId);
}

std::vector<LaunchItem> noteDragPreviewItems(AppContext& context)
{
    std::vector<std::string> ids = activeDragNoteIdsForDrop();
    if (ids.empty()) {
        return {};
    }

    std::vector<LaunchItem> items;
    items.reserve(ids.size());
    std::unordered_set<std::string> seen;
    for (const std::string& id : ids) {
        if (!seen.insert(id).second) {
            continue;
        }
        const Note* note = context.notes.find(id);
        if (!note) {
            continue;
        }
        LaunchItem item;
        item.id = "note-drag-preview-" + note->id;
        item.name = NotesStore::displayTitle(*note);
        item.subtitle = formatNoteTags(note->tags).empty() ? tr("Notes") : formatNoteTags(note->tags);
        item.target = note->id;
        item.type = LaunchItemType::Note;
        item.fallbackColor = "#6A9A7CFF";
        item.createdAt = note->createdAt;
        item.lastEditedAt = note->updatedAt;
        items.push_back(std::move(item));
    }
    return items;
}

std::vector<const LaunchItem*> itemPointers(const std::vector<LaunchItem>& items)
{
    std::vector<const LaunchItem*> pointers;
    pointers.reserve(items.size());
    for (const LaunchItem& item : items) {
        pointers.push_back(&item);
    }
    return pointers;
}

void focusMainWindowForNoteDrag()
{
    if (gMainWindowFocusedForNoteDrag) {
        return;
    }
    gMainWindowFocusedForNoteDrag = true;
    if (ImGuiWindow* window = ImGui::GetCurrentWindow()) {
        ImGui::FocusWindow(window->RootWindow);
    }
    bringWindowToDialogFront(mainWindowHandle());
}

void rememberItemDisplayPosition(const LaunchItem& item, const ImVec2& pos)
{
    gLastItemDisplayPositions[item.id] = pos;
}

ImVec2 rememberedItemDisplayPosition(const LaunchItem& item, const ImVec2& fallback)
{
    const auto it = gLastItemDisplayPositions.find(item.id);
    return it != gLastItemDisplayPositions.end() ? it->second : fallback;
}

const Category* selectedCategory(const AppContext& context)
{
    const PersistedState& persisted = context.persisted();
    const RuntimeState& runtime = context.runtime();
    if (runtime.selectedCategory < 0 || runtime.selectedCategory >= static_cast<int>(persisted.categories.size())) {
        return nullptr;
    }
    return &persisted.categories[runtime.selectedCategory];
}

ItemViewMode effectiveViewMode(const AppContext& context)
{
    const Category* category = selectedCategory(context);
    if (category && !category->useGlobalLayout) {
        return category->viewMode;
    }
    return context.persisted().settings.viewMode;
}

int effectiveIconSize(const AppContext& context)
{
    const Category* category = selectedCategory(context);
    if (category && !category->useGlobalLayout) {
        return category->iconSize;
    }
    return context.persisted().settings.iconSize;
}

std::string draggedPayloadItemId(const MainDockGridApi& api)
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->IsDataType(drag_payload::kItemId) || !api.dragPayloadId) {
        return {};
    }
    return api.dragPayloadId(payload);
}

float titleRowHeight(const LaunchItem& item)
{
    return itemViewTitleRowHeight(item);
}

ImU32 themeColor(const ImVec4& color)
{
    return ImGui::ColorConvertFloat4ToU32(color);
}

void drawTitleGhost(const UiPalette& theme, const LaunchItem& item, const ImVec2& pos, float width)
{
    const float height = titleRowHeight(item);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 max(pos.x + width, pos.y + height);
    const float alpha = ui_anim::ghostAmount(ImGui::GetID(("title-ghost-" + item.id).c_str()));
    dl->AddRectFilled(
        pos, max,
        ImGui::ColorConvertFloat4ToU32(ImVec4(theme.headerHovered.x, theme.headerHovered.y, theme.headerHovered.z, 0.32f * alpha)),
        theme.itemRounding);
    ImVec4 border = theme.frameActive;
    border.w *= alpha;
    dl->AddRect(pos, max, themeColor(border), theme.itemRounding, 0, 1.5f);
    const float fontSize = static_cast<float>(std::clamp(item.titleSize, 12, 48));
    const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, item.name.c_str());
    ImVec4 textColor = ImGui::ColorConvertU32ToFloat4(theme.text);
    textColor.w *= alpha;
    dl->AddText(nullptr, fontSize, ImVec2(pos.x + std::max(8.0f, (width - textSize.x) * 0.5f), pos.y + (height - textSize.y) * 0.5f),
                themeColor(textColor), item.name.c_str());
}

void drawItemGhostSlot(const UiPalette& theme, const LaunchItem& item, const ImVec2& pos, const ImVec2& size)
{
    const float alpha = ui_anim::ghostAmount(ImGui::GetID(("item-ghost-" + item.id).c_str()));
    ImVec4 fill = theme.headerHovered;
    fill.w *= 0.20f * alpha;
    ImVec4 border = theme.frameActive;
    border.w *= alpha;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), themeColor(fill), theme.itemRounding);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), themeColor(border), theme.itemRounding, 0, 1.5f);
}

std::vector<ImVec2> dragPreviewPositions(ItemViewMode viewMode, int count, const ImVec2& anchor)
{
    const bool horizontal = viewMode != ItemViewMode::Icon;
    const ImVec2 step = horizontal ? ImVec2(12.0f, 8.0f) : ImVec2(14.0f, 12.0f);
    std::vector<ImVec2> positions;
    positions.reserve(std::max(1, count));
    const int layers = std::max(1, std::min(count, 3));
    for (int i = 0; i < layers; ++i) {
        positions.push_back(ImVec2(anchor.x + i * step.x, anchor.y + i * step.y));
    }
    while (static_cast<int>(positions.size()) < count) {
        positions.push_back(positions.back());
    }
    return positions;
}

ImVec2 smoothDraggedPreviewAnchor(const ImVec2& target)
{
    if (!ui_anim::enabled()) {
        gActiveDraggedPreviewAnchor = target;
        return target;
    }
    if (!gActiveDraggedPreviewAnchor.has_value()) {
        gActiveDraggedPreviewAnchor = target;
        return target;
    }

    const ImVec2 current = *gActiveDraggedPreviewAnchor;
    const ImVec2 delta(target.x - current.x, target.y - current.y);
    const float distanceSq = delta.x * delta.x + delta.y * delta.y;
    if (distanceSq <= 0.04f) {
        gActiveDraggedPreviewAnchor = target;
        return target;
    }

    const float distance = std::sqrt(distanceSq);
    const float dt = std::clamp(ui_anim::dt(), 1.0f / 240.0f, 1.0f / 30.0f);
    const float followBoost = std::clamp((distance - 10.0f) / 120.0f, 0.0f, 1.0f);
    const float followRate = 24.0f + followBoost * 48.0f;
    const float alpha = 1.0f - std::exp(-followRate * dt);

    ImVec2 next(current.x + delta.x * alpha, current.y + delta.y * alpha);

    constexpr float kMaxDragPreviewLag = 42.0f;
    const ImVec2 remaining(target.x - next.x, target.y - next.y);
    const float remainingSq = remaining.x * remaining.x + remaining.y * remaining.y;
    if (remainingSq > kMaxDragPreviewLag * kMaxDragPreviewLag) {
        const float remainingDistance = std::sqrt(remainingSq);
        const float scale = kMaxDragPreviewLag / remainingDistance;
        next = ImVec2(target.x - remaining.x * scale, target.y - remaining.y * scale);
    }

    gActiveDraggedPreviewAnchor = next;
    return next;
}

void storePendingDropPreviewPositions(const std::vector<const LaunchItem*>& items, ItemViewMode viewMode)
{
    if (items.empty()) {
        return;
    }
    const ImVec2 anchor(ImGui::GetIO().MousePos.x + 14.0f, ImGui::GetIO().MousePos.y + 14.0f);
    const std::vector<ImVec2> positions = dragPreviewPositions(viewMode, static_cast<int>(items.size()), anchor);
    for (int i = 0; i < static_cast<int>(items.size()); ++i) {
        gPendingDropPreviewPositions[items[i]->id] = positions[i];
    }
}

void storePendingDropPreviewPositionsFromActiveSession()
{
    if (gActiveDraggedCurrentPreviewPositions.empty()) {
        return;
    }
    for (const auto& [id, pos] : gActiveDraggedCurrentPreviewPositions) {
        gPendingDropPreviewPositions[id] = pos;
    }
}

void applyPendingDropLayoutStart(const LaunchItem& item, const char* layoutChannel)
{
    const auto it = gPendingDropPreviewPositions.find(item.id);
    if (it == gPendingDropPreviewPositions.end()) {
        return;
    }
    ImGui::PushID(item.id.c_str());
    ui_anim::snapLayoutPos(ImGui::GetID(layoutChannel), it->second);
    ImGui::PopID();
    gPendingDropPreviewPositions.erase(it);
}

bool moveDragIdsIntoFolder(AppContext& context, MainDockGridState state, const MainDockGridApi& api, const LaunchItem& folder,
                           const std::vector<std::string>& dragIds)
{
    if (!state.folderStack || !api.itemsForFolderStack || !api.moveItemByIdToList || folder.type != LaunchItemType::VirtualFolder ||
        folder.lockLayout || dragIds.empty()) {
        return false;
    }
    if (std::find(dragIds.begin(), dragIds.end(), folder.id) != dragIds.end()) {
        return false;
    }

    std::vector<std::string> targetStack = *state.folderStack;
    if (targetStack.empty() || targetStack.back() != folder.id) {
        targetStack.push_back(folder.id);
    }
    std::vector<LaunchItem>* destination = api.itemsForFolderStack(context, targetStack);
    if (!destination) {
        return false;
    }

    captureActiveDragPreviewForDrop(context);
    if (dragIds.size() > 1 && api.moveItemIdsToList) {
        return api.moveItemIdsToList(context, dragIds, *destination, -1);
    }
    const std::string& primaryId = dragIds.front();
    return !primaryId.empty() && api.moveItemByIdToList(context, primaryId, *destination, -1);
}

bool acceptDropIntoFolder(AppContext& context, MainDockGridState state, const MainDockGridApi& api, const ImRect& rect,
                          const LaunchItem& folder, const std::vector<std::string>& dragIds)
{
    if (dragIds.empty() || folder.type != LaunchItemType::VirtualFolder ||
        std::find(dragIds.begin(), dragIds.end(), folder.id) != dragIds.end()) {
        return false;
    }
    const std::string targetId = "folder-drop-" + folder.id;
    if (!ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID(targetId.c_str()))) {
        return false;
    }

    bool delivered = false;
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(drag_payload::kItemId, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
        if (payload->IsDelivery()) {
            delivered = moveDragIdsIntoFolder(context, state, api, folder, dragIds);
        }
    }
    ImGui::EndDragDropTarget();
    return delivered;
}

bool moveDragIdsToList(AppContext& context, const MainDockGridApi& api, const std::vector<std::string>& dragIds,
                       std::vector<LaunchItem>& items, int insertAt)
{
    if (dragIds.empty() || !api.moveItemByIdToList) {
        return false;
    }
    captureActiveDragPreviewForDrop(context);
    if (dragIds.size() > 1 && api.moveItemIdsToList) {
        return api.moveItemIdsToList(context, dragIds, items, insertAt);
    }
    return api.moveItemByIdToList(context, dragIds.front(), items, insertAt);
}

bool dragIdsBelongToList(const std::vector<LaunchItem>& items, const std::vector<std::string>& dragIds)
{
    if (dragIds.empty()) {
        return false;
    }
    std::unordered_set<std::string> dragged(dragIds.begin(), dragIds.end());
    for (const LaunchItem& item : items) {
        if (dragged.contains(item.id)) {
            return true;
        }
    }
    return false;
}

void drawDraggedItemPreview(const UiPalette& theme, const ItemViewApi& itemApi, const std::vector<const LaunchItem*>& items,
                            ItemViewMode viewMode, float iconSize, float tileW, float tileH, float rowHeight)
{
    if (items.empty()) {
        return;
    }
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const ImVec2 targetAnchor(ImGui::GetIO().MousePos.x + 14.0f, ImGui::GetIO().MousePos.y + 14.0f);
    const ImVec2 anchor = smoothDraggedPreviewAnchor(targetAnchor);
    const bool horizontal = viewMode != ItemViewMode::Icon;
    const int layers = std::max(1, std::min(static_cast<int>(items.size()), 3));
    const std::vector<ImVec2> positions = dragPreviewPositions(viewMode, layers, anchor);
    for (int layer = layers - 1; layer >= 0; --layer) {
        const LaunchItem& item = *items[layer];
        ImVec2 size = horizontal ? ImVec2(220.0f, std::max(48.0f, rowHeight - 4.0f)) : ImVec2(tileW, tileH);
        if (item.type == LaunchItemType::Title) {
            size = ImVec2(260.0f, titleRowHeight(item));
        }
        const ImVec2 pos = positions[layer];
        gActiveDraggedCurrentPreviewPositions[item.id] = pos;

        ImVec4 bg = theme.headerHovered;
        bg.w = std::max(0.62f, 0.88f - (layers - 1 - layer) * 0.10f);
        dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), themeColor(bg), theme.itemRounding);
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), themeColor(theme.frameActive), theme.itemRounding, 0, 1.2f);

        if (item.type == LaunchItemType::Title) {
            const float fontSize = static_cast<float>(std::clamp(item.titleSize, 12, 48));
            const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, item.name.c_str());
            dl->AddText(nullptr, fontSize, ImVec2(pos.x + 12.0f, pos.y + (size.y - textSize.y) * 0.5f), theme.text, item.name.c_str());
            continue;
        }

        const float previewIconSize = horizontal ? std::min(iconSize, 32.0f) : iconSize;
        if (itemApi.drawLaunchIconOnList) {
            const ImVec2 iconPos = horizontal ? ImVec2(pos.x + 10.0f, pos.y + (size.y - previewIconSize) * 0.5f)
                                              : ImVec2(pos.x + (size.x - previewIconSize) * 0.5f, pos.y + 10.0f);
            itemApi.drawLaunchIconOnList(dl, item, iconPos, previewIconSize);
        }
        if (horizontal) {
            dl->AddText(ImVec2(pos.x + 24.0f + previewIconSize, pos.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f), theme.text,
                        item.name.c_str());
            continue;
        }
        const ImVec2 textSize = ImGui::CalcTextSize(item.name.c_str());
        dl->AddText(ImVec2(pos.x + std::max(4.0f, (size.x - textSize.x) * 0.5f), pos.y + previewIconSize + 18.0f), theme.text,
                    item.name.c_str());
    }
}

bool acceptAppendDrop(AppContext& context, const MainDockGridApi& api, std::vector<LaunchItem>& items, const ImRect& rect, const char* id)
{
    const AppSettings& settings = context.persisted().settings;
    const bool itemDropOk = !settings.lockItemLayout && settings.sortMode == SortMode::Free && api.moveItemByIdToList &&
                            api.dragPayloadId && !currentListLayoutLocked(context);
    // Notes can always drop into the current list to create note items.
    if (!ImGui::BeginDragDropTargetCustom(rect, ImGui::GetID(id))) {
        return false;
    }
    bool delivered = false;
    if (itemDropOk) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(drag_payload::kItemId, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
            if (payload->IsDelivery()) {
                delivered = moveDragIdsToList(context, api, draggedPayloadItemIds(api, context), items, -1);
            }
        }
    }
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(drag_payload::kNoteId, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
        if (payload->IsDelivery()) {
            std::vector<std::string> ids = activeDragNoteIdsForDrop();
            if (ids.empty() && payload->Data && payload->DataSize > 0) {
                ids.emplace_back(static_cast<const char*>(payload->Data));
            }
            delivered = addNoteIdsAsListItems(context, ids) || delivered;
        }
    }
    ImGui::EndDragDropTarget();
    return delivered;
}

bool fallbackDropIntoCurrentFolder(AppContext& context, MainDockGridState state, const MainDockGridApi& api, std::vector<LaunchItem>& items,
                                   const ImRect& contentRect)
{
    const AppSettings& settings = context.persisted().settings;
    if (!state.folderStack || state.folderStack->empty() || settings.lockItemLayout || settings.sortMode != SortMode::Free ||
        currentListLayoutLocked(context) || !contentRect.Contains(ImGui::GetIO().MousePos) ||
        !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        return false;
    }
    return moveDragIdsToList(context, api, activeOrReleasedDragIds(api, context), items, -1);
}

} // namespace

void drawItemGrid(const UiPalette& theme, AppContext& context, MainDockGridState state, const MainDockGridApi& api, const ImVec2& origin,
                  const ImVec2& size)
{
    const AppSettings& settings = context.persisted().settings;
    syncActiveDraggedPreviewItems(api, context);
    const ImGuiPayload* activeDragPayload = ImGui::GetDragDropPayload();
    const bool noteDragActive = noteDragPayloadActive();
    if (!noteDragActive) {
        gMainWindowFocusedForNoteDrag = false;
    }
    if (!activeDragPayload || !activeDragPayload->IsDataType(drag_payload::kItemId)) {
        clearDragItemIdsSnapshot();
        // Reset pending hover-open targets when no item drag is active.
        if (api.triggerAfterDragHover) {
            api.triggerAfterDragHover("", [] {});
        }
    }

    ImGui::SetCursorScreenPos(origin);
    const bool hasBackgroundImage = context.themes.active().background.enabled && !context.themes.active().background.imagePath.empty();
    const ImVec4 childBg(0.0f, 0.0f, 0.0f, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, childBg);
    ImGui::BeginChild("items", size, ImGuiChildFlags_None);
    if (noteDragActive && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
        focusMainWindowForNoteDrag();
    }
    // Always expose a canvas-wide note-drop target so notes can be dragged in even on empty lists.
    if (api.currentItems) {
        if (std::vector<LaunchItem>* items = api.currentItems(context)) {
            acceptAppendDrop(context, api, *items,
                             ImRect(ImGui::GetWindowPos(), ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x,
                                                                  ImGui::GetWindowPos().y + ImGui::GetWindowSize().y)),
                             "note-to-item-canvas-drop");
        }
    }
    if (state.resetMainDockScroll && *state.resetMainDockScroll) {
        ImGui::SetScrollY(0.0f);
        *state.resetMainDockScroll = false;
    }
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    if (!hasBackgroundImage) {
        ImGui::GetWindowDrawList()->AddRectFilled(canvasOrigin, ImVec2(canvasOrigin.x + size.x, canvasOrigin.y + size.y), theme.contentBg,
                                                  theme.windowRounding, ImDrawFlags_RoundCornersBottomRight);
    }
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::IsAnyItemHovered()) {
        if (api.clearSelection) {
            api.clearSelection(context);
        }
    }
    if (settings.hideOnBlankDoubleClick && ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
        !ImGui::IsAnyItemHovered()) {
        if (api.requestHideMainWindow) {
            api.requestHideMainWindow();
        }
    }
    const bool openBlankMenu = !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::IsWindowHovered() &&
                               ImGui::IsMouseClicked(ImGuiMouseButton_Right);

    if (!api.currentItems) {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }
    std::vector<LaunchItem>* items = api.currentItems(context);
    if (!items) {
        ImGui::TextUnformatted("No category");
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    const ItemViewMode viewMode = effectiveViewMode(context);
    const float iconSize = static_cast<float>(std::clamp(effectiveIconSize(context), 24, 96));
    std::vector<LaunchItem> notePreviewStorage = noteDragActive ? noteDragPreviewItems(context) : std::vector<LaunchItem>{};
    std::vector<const LaunchItem*> notePreviewItems = itemPointers(notePreviewStorage);
    std::vector<int> order = orderedIndices(*items, settings.sortMode);
    float topOffset = 20.0f;
    bool navigationDropDelivered = false;
    const std::vector<std::string> initialFolderStack = state.folderStack ? *state.folderStack : std::vector<std::string>{};
    auto acceptDropToStack = [&](const std::vector<std::string>& stack) -> bool {
        bool delivered = false;
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(drag_payload::kItemId, ImGuiDragDropFlags_AcceptPeekOnly)) {
                const std::string id = api.dragPayloadId ? api.dragPayloadId(payload) : std::string{};
                if (std::find(stack.begin(), stack.end(), id) == stack.end()) {
                    if (api.triggerAfterDragHover) {
                        api.triggerAfterDragHover(stackKey(stack), [&]() {
                            if (state.folderStack && *state.folderStack != stack) {
                                *state.folderStack = stack;
                                if (api.clearSelection) {
                                    api.clearSelection(context);
                                }
                            }
                        });
                    }
                    if (api.itemsForFolderStack && !listLayoutLockedForStack(context, stack)) {
                        if (std::vector<LaunchItem>* destination = api.itemsForFolderStack(context, stack)) {
                            if (payload->IsDelivery() && api.moveItemByIdToList) {
                                captureActiveDragPreviewForDrop(context);
                                const std::vector<std::string> dragIds =
                                    api.dragItemIds ? api.dragItemIds(context, id) : std::vector<std::string>{id};
                                if (dragIds.size() > 1 && api.moveItemIdsToList) {
                                    delivered = api.moveItemIdsToList(context, dragIds, *destination, -1);
                                } else {
                                    delivered = api.moveItemByIdToList(context, id, *destination, -1);
                                }
                            }
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        return delivered;
    };

    if (state.folderStack && !state.folderStack->empty()) {
        ImGui::SetCursorScreenPos(ImVec2(canvasOrigin.x + 12.0f, canvasOrigin.y + 8.0f));
        ImGui::BeginGroup();
        auto navIconButton = [&](const char* id, const char* icon, bool enabled = true) -> bool {
            ImGui::PushID(id);
            ImGui::BeginDisabled(!enabled);
            const bool clicked = ImGui::InvisibleButton("nav-icon", ImVec2(28.0f, 28.0f));
            ImGui::EndDisabled();
            const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            const bool hovered = enabled && ImGui::IsItemHovered();
            if (hovered || ImGui::IsItemActive() || (api.dragHoverPending && api.dragHoverPending(id))) {
                ImGui::GetWindowDrawList()->AddRectFilled(rect.Min, rect.Max,
                                                          themeColor(hovered ? theme.headerHovered : theme.headerActive), 4.0f);
                if (api.dragHoverPending && api.dragHoverPending(id)) {
                    ImGui::GetWindowDrawList()->AddRect(rect.Min, rect.Max, themeColor(theme.frameActive), 4.0f, 0, 1.5f);
                }
            }
            const ImVec2 textSize = ImGui::CalcTextSize(icon);
            ImGui::GetWindowDrawList()->AddText(ImVec2(rect.Min.x + (28.0f - textSize.x) * 0.5f, rect.Min.y + (28.0f - textSize.y) * 0.5f),
                                                enabled ? theme.text : theme.textMuted, icon);
            ImGui::PopID();
            ImGui::SameLine(0.0f, 4.0f);
            return clicked && enabled;
        };
        auto navTextButton = [&](const char* id, const char* text) -> bool {
            const ImVec2 textSize = ImGui::CalcTextSize(text);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.headerHovered);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.headerActive);
            const bool clicked = ImGui::Button((std::string(text) + "##" + id).c_str(), ImVec2(textSize.x + 16.0f, 28.0f));
            ImGui::PopStyleColor(3);
            if (api.dragHoverPending && api.dragHoverPending(id)) {
                const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                ImGui::GetWindowDrawList()->AddRect(rect.Min, rect.Max, themeColor(theme.frameActive), 4.0f, 0, 1.5f);
            }
            ImGui::SameLine(0.0f, 4.0f);
            return clicked;
        };
        if (navIconButton("folder-back", Icons::Back, !state.folderStack->empty())) {
            state.folderStack->pop_back();
            if (api.clearSelection) {
                api.clearSelection(context);
            }
        }
        navIconButton("folder-forward", Icons::Forward, false);
        if (navIconButton("folder-home", Icons::Home, !state.folderStack->empty())) {
            state.folderStack->clear();
            if (api.clearSelection) {
                api.clearSelection(context);
            }
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(drag_payload::kItemId, ImGuiDragDropFlags_AcceptPeekOnly)) {
                if (api.triggerAfterDragHover) {
                    api.triggerAfterDragHover("folder-home", [&]() {
                        if (state.folderStack) {
                            state.folderStack->clear();
                        }
                        if (api.clearSelection) {
                            api.clearSelection(context);
                        }
                    });
                }
                if (api.dragHoverPending && api.dragHoverPending("folder-home")) {
                    const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
                    ImGui::GetWindowDrawList()->AddRect(rect.Min, rect.Max, themeColor(theme.frameActive), 4.0f, 0, 1.5f);
                }
                const std::string id = api.dragPayloadId ? api.dragPayloadId(payload) : std::string{};
                if (payload->IsDelivery() && !id.empty() && api.itemsForFolderStack && api.moveItemByIdToList &&
                    !listLayoutLockedForStack(context, {})) {
                    if (std::vector<LaunchItem>* destination = api.itemsForFolderStack(context, {})) {
                        captureActiveDragPreviewForDrop(context);
                        const std::vector<std::string> dragIds =
                            api.dragItemIds ? api.dragItemIds(context, id) : std::vector<std::string>{id};
                        if (dragIds.size() > 1 && api.moveItemIdsToList) {
                            navigationDropDelivered = api.moveItemIdsToList(context, dragIds, *destination, -1) || navigationDropDelivered;
                        } else {
                            navigationDropDelivered = api.moveItemByIdToList(context, id, *destination, -1) || navigationDropDelivered;
                        }
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        std::vector<std::string> prefix;
        for (const std::string& id : *state.folderStack) {
            prefix.push_back(id);
            if (api.findItemById) {
                if (const LaunchItem* folder = api.findItemById(context, id)) {
                    ImGui::SameLine();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled(">");
                    ImGui::SameLine(0.0f, 4.0f);
                    const std::string buttonId = "folder-path-" + folder->id;
                    if (navTextButton(buttonId.c_str(), folder->name.c_str())) {
                        *state.folderStack = prefix;
                        if (api.clearSelection) {
                            api.clearSelection(context);
                        }
                    }
                    navigationDropDelivered = acceptDropToStack(prefix) || navigationDropDelivered;
                }
            }
        }
        ImGui::EndGroup();
        topOffset = 44.0f;
    }
    if (navigationDropDelivered || (state.folderStack && *state.folderStack != initialFolderStack)) {
        items = api.currentItems(context);
        if (!items) {
            ImGui::EndChild();
            ImGui::PopStyleColor();
            return;
        }
        order = orderedIndices(*items, settings.sortMode);
    }
    const bool listLocked = currentListLayoutLocked(context);

    const ImVec2 start(canvasOrigin.x + 34.0f, canvasOrigin.y + topOffset);
    const float availableWidth = std::max(0.0f, size.x - 44.0f);

    if (viewMode == ItemViewMode::List) {
        const int nameLines = std::clamp(effectiveNameLines(context), 1, 3);
        const float textLineHeight = ImGui::GetTextLineHeight();
        const float rowHeight = std::max({48.0f, iconSize + 14.0f, textLineHeight * static_cast<float>(nameLines + 1) + 16.0f});
        const std::vector<std::string> payloadDraggedIds = activeOrReleasedDragIds(api, context);
        const std::unordered_set<std::string> payloadDraggedIdSet(payloadDraggedIds.begin(), payloadDraggedIds.end());
        const std::vector<int> draggedIndices = draggedIndicesInDisplayOrder(*items, payloadDraggedIds);
        const std::unordered_set<int> draggedSet = draggedIndexSet(draggedIndices);
        std::vector<const LaunchItem*> draggedPreviewItems = activeDraggedPreviewItems();
        if (noteDragActive) {
            draggedPreviewItems = notePreviewItems;
        }
        int ghostInsert =
            (!settings.lockItemLayout && !listLocked && settings.sortMode == SortMode::Free)
                ? ghostInsertIndexForCurrentListRows(api, *items, order, draggedSet, start.x, availableWidth - 8.0f, start.y, rowHeight)
                : -1;
        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        bool deliveredThisFrame = false;
        if (payload && payload->IsDataType(drag_payload::kItemId) && payload->IsDelivery() && api.dragPayloadId &&
            !settings.lockItemLayout && !listLocked && settings.sortMode == SortMode::Free) {
            const std::string deliveredId = api.dragPayloadId(payload);
            const ImRect appendRect(start, ImVec2(start.x + availableWidth - 8.0f, canvasOrigin.y + size.y));
            const int folderDropIndex =
                hoveredFolderDropIndexForRows(*items, order, draggedSet, start.x, availableWidth - 8.0f, start.y, rowHeight);
            if (folderDropIndex >= 0) {
                deliveredThisFrame = moveDragIdsIntoFolder(context, state, api, (*items)[folderDropIndex], payloadDraggedIds);
            } else if (ghostInsert >= 0) {
                deliveredThisFrame = moveDragIdsToList(context, api, payloadDraggedIds, *items, ghostInsert);
            } else if (!dragIdsBelongToList(*items, payloadDraggedIds) && appendRect.Contains(ImGui::GetIO().MousePos)) {
                deliveredThisFrame = moveDragIdsToList(context, api, payloadDraggedIds, *items, -1);
            }
            if (deliveredThisFrame) {
                order = orderedIndices(*items, settings.sortMode);
                ghostInsert = -1;
            }
        }
        const std::vector<int> displayOrder =
            ghostInsert >= 0 ? orderWithGhostSlot(order, draggedSet, ghostInsert, std::max(1, static_cast<int>(draggedPreviewItems.size())))
                             : order;
        int ghostOrdinal = 0;
        float cursorY = start.y;
        for (int display = 0; display < static_cast<int>(displayOrder.size()); ++display) {
            if (displayOrder[display] < 0) {
                const ImVec2 row(start.x, cursorY);
                const LaunchItem* ghostItem = ghostOrdinal < static_cast<int>(draggedPreviewItems.size())
                                                  ? draggedPreviewItems[ghostOrdinal]
                                                  : (!draggedPreviewItems.empty() ? draggedPreviewItems.back() : nullptr);
                if (ghostItem && ghostItem->type == LaunchItemType::Title) {
                    drawTitleGhost(theme, *ghostItem, row, availableWidth - 8.0f);
                    cursorY += titleRowHeight(*ghostItem);
                } else {
                    if (ghostItem) {
                        drawItemGhostSlot(theme, *ghostItem, row, ImVec2(availableWidth - 8.0f, rowHeight - 4.0f));
                    }
                    cursorY += rowHeight;
                }
                ++ghostOrdinal;
                continue;
            }
            const int itemIndex = displayOrder[display];
            if (!deliveredThisFrame && payloadDraggedIdSet.contains((*items)[itemIndex].id)) {
                continue;
            }
            LaunchItem& item = (*items)[itemIndex];
            const float currentRowHeight = item.type == LaunchItemType::Title ? titleRowHeight(item) : rowHeight;
            const ImVec2 row(start.x, cursorY);
            if (item.type == LaunchItemType::Title) {
                applyPendingDropLayoutStart(item, "title-layout");
                drawTitleRow(theme, context, api.itemViewApi, *items, itemIndex, row, availableWidth - 8.0f);
            } else {
                applyPendingDropLayoutStart(item, "row-layout");
                drawListItem(theme, context, api.itemViewApi, *items, itemIndex, row, availableWidth - 8.0f, currentRowHeight - 4.0f,
                             std::min(iconSize, 32.0f));
            }
            const ImVec2 visualItemPos = rememberedItemDisplayPosition(item, row);
            if (!listLocked && !deliveredThisFrame && item.type == LaunchItemType::VirtualFolder && !payloadDraggedIds.empty() &&
                std::find(payloadDraggedIds.begin(), payloadDraggedIds.end(), item.id) == payloadDraggedIds.end() &&
                listFolderAutoEnterZone(visualItemPos, availableWidth - 8.0f, currentRowHeight - 4.0f).Contains(ImGui::GetIO().MousePos)) {
                const std::string targetKey = "folder-item:" + item.id;
                if (api.triggerAfterDragHover) {
                    api.triggerAfterDragHover(targetKey, [&]() {
                        if (api.itemViewApi.enterVirtualFolder) {
                            api.itemViewApi.enterVirtualFolder(context, item);
                        }
                    });
                }
                if (api.dragHoverPending && api.dragHoverPending(targetKey)) {
                    ImGui::GetWindowDrawList()->AddRect(
                        visualItemPos, ImVec2(visualItemPos.x + availableWidth - 8.0f, visualItemPos.y + currentRowHeight - 4.0f),
                        themeColor(theme.frameActive), 6.0f, 0, 1.5f);
                }
            }
            const bool droppedIntoFolder =
                !listLocked && !deliveredThisFrame && item.type == LaunchItemType::VirtualFolder && !payloadDraggedIds.empty() &&
                acceptDropIntoFolder(context, state, api, listFolderDropZone(visualItemPos, availableWidth - 8.0f, currentRowHeight - 4.0f),
                                     item, payloadDraggedIds);
            cursorY += currentRowHeight;
            if (droppedIntoFolder) {
                deliveredThisFrame = true;
                break;
            }
        }
        if (!deliveredThisFrame && ghostInsert >= 0) {
            const ImRect dropRect(start, ImVec2(start.x + availableWidth - 8.0f, canvasOrigin.y + size.y));
            if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("list-reorder-drop-zone"))) {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload(drag_payload::kItemId, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                    if (payload->IsDelivery() && api.dragPayloadId) {
                        deliveredThisFrame = moveDragIdsToList(context, api, activeOrReleasedDragIds(api, context), *items, ghostInsert);
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        if (!deliveredThisFrame && !payloadDraggedIds.empty()) {
            if (state.folderStack && !state.folderStack->empty()) {
                if (!dragIdsBelongToList(*items, payloadDraggedIds)) {
                    deliveredThisFrame = acceptAppendDrop(context, api, *items,
                                                          ImRect(canvasOrigin, ImVec2(canvasOrigin.x + size.x, canvasOrigin.y + size.y)),
                                                          "list-current-folder-drop-zone");
                }
                if (!deliveredThisFrame) {
                    deliveredThisFrame = fallbackDropIntoCurrentFolder(
                        context, state, api, *items, ImRect(start, ImVec2(start.x + availableWidth - 8.0f, canvasOrigin.y + size.y)));
                }
            }
            if (!deliveredThisFrame) {
                if (!dragIdsBelongToList(*items, payloadDraggedIds)) {
                    deliveredThisFrame = acceptAppendDrop(context, api, *items,
                                                          ImRect(start, ImVec2(start.x + availableWidth - 8.0f, canvasOrigin.y + size.y)),
                                                          "list-append-drop-zone");
                }
            }
        }
        if (openBlankMenu && !ImGui::IsAnyItemHovered()) {
            ImGui::OpenPopup("content-menu");
        }
        if (api.drawContentMenu && api.contentMenuState) {
            api.drawContentMenu(theme, context, api.contentMenuState(), api.contentMenuApi);
        }
        if (!deliveredThisFrame && !draggedPreviewItems.empty()) {
            drawDraggedItemPreview(theme, api.itemViewApi, draggedPreviewItems, ItemViewMode::List, iconSize, availableWidth - 8.0f,
                                   rowHeight - 4.0f, rowHeight);
        }
        ImGui::SetCursorScreenPos(ImVec2(start.x, cursorY));
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
        ImGui::EndChild();
        ImGui::PopStyleColor();
        return;
    }

    const bool tileMode = viewMode == ItemViewMode::Tile;
    const int nameLines = std::clamp(effectiveNameLines(context), 0, 3);
    const float textLineHeight = ImGui::GetTextLineHeight();
    const float iconPadding = std::max(6.0f, iconSize * 0.15f);
    const float iconTextHeight =
        iconPadding + iconSize + (nameLines > 0 ? 6.0f + textLineHeight * static_cast<float>(nameLines) : 0.0f) + 12.0f;
    const float tileW = tileMode ? std::max(120.0f, iconSize + 92.0f) : std::clamp(iconSize + 40.0f, 72.0f, 128.0f);
    const float tileH = tileMode
                            ? std::max({52.0f, iconSize + 26.0f, textLineHeight * static_cast<float>(std::max(1, nameLines) + 1) + 18.0f})
                            : std::max(std::clamp(iconSize + 46.0f, 76.0f, 136.0f), iconTextHeight);
    const float gapX = iconSize <= 40.0f ? 6.0f : iconSize >= 56.0f ? 18.0f : 12.0f;
    const float gapY = iconSize <= 40.0f ? 2.0f : iconSize >= 56.0f ? 12.0f : 6.0f;
    const int columns = std::max(1, static_cast<int>(availableWidth / (tileW + gapX)));
    const std::vector<std::string> payloadDraggedIds = draggedPayloadItemIds(api, context);
    const std::unordered_set<std::string> payloadDraggedIdSet(payloadDraggedIds.begin(), payloadDraggedIds.end());
    const std::vector<int> draggedIndices = draggedIndicesInDisplayOrder(*items, payloadDraggedIds);
    const std::unordered_set<int> draggedSet = draggedIndexSet(draggedIndices);
    std::vector<const LaunchItem*> draggedPreviewItems = activeDraggedPreviewItems();
    if (noteDragActive) {
        draggedPreviewItems = notePreviewItems;
    }
    const bool draggingTitle = !draggedPreviewItems.empty() && draggedPreviewItems.front()->type == LaunchItemType::Title;
    int ghostInsert =
        (!settings.lockItemLayout && settings.sortMode == SortMode::Free)
            ? (!listLocked ? ghostInsertIndexForCurrentList(api, *items, order, draggedSet, start.x, start.y, availableWidth - 8.0f, tileW,
                                                            tileH, gapX, gapY, columns, draggingTitle)
                           : -1)
            : -1;
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    bool deliveredThisFrame = false;
    if (payload && payload->IsDataType(drag_payload::kItemId) && payload->IsDelivery() && api.dragPayloadId && !settings.lockItemLayout &&
        !listLocked && settings.sortMode == SortMode::Free) {
        const ImRect appendRect(start, ImVec2(start.x + availableWidth - 8.0f, canvasOrigin.y + size.y));
        const int folderDropIndex =
            hoveredFolderDropIndexForGrid(*items, order, draggedSet, start.x, start.y, tileW, tileH, gapX, gapY, columns);
        if (folderDropIndex >= 0) {
            deliveredThisFrame = moveDragIdsIntoFolder(context, state, api, (*items)[folderDropIndex], payloadDraggedIds);
        } else if (ghostInsert >= 0) {
            deliveredThisFrame = moveDragIdsToList(context, api, payloadDraggedIds, *items, ghostInsert);
        } else if (!dragIdsBelongToList(*items, payloadDraggedIds) && appendRect.Contains(ImGui::GetIO().MousePos)) {
            deliveredThisFrame = moveDragIdsToList(context, api, payloadDraggedIds, *items, -1);
        }
        if (deliveredThisFrame) {
            order = orderedIndices(*items, settings.sortMode);
            ghostInsert = -1;
        }
    }
    const std::vector<int> displayOrder =
        ghostInsert >= 0 ? orderWithGhostSlot(order, draggedSet, ghostInsert, std::max(1, static_cast<int>(draggedPreviewItems.size())))
                         : order;
    int visualColumn = 0;
    float cursorY = start.y;
    int ghostOrdinal = 0;
    for (int display = 0; display < static_cast<int>(displayOrder.size()); ++display) {
        if (displayOrder[display] < 0) {
            const LaunchItem* ghostItem = ghostOrdinal < static_cast<int>(draggedPreviewItems.size())
                                              ? draggedPreviewItems[ghostOrdinal]
                                              : (!draggedPreviewItems.empty() ? draggedPreviewItems.back() : nullptr);
            if (ghostItem && ghostItem->type == LaunchItemType::Title) {
                if (visualColumn != 0) {
                    visualColumn = 0;
                    cursorY += tileH + gapY;
                }
                const ImVec2 row(start.x, cursorY);
                if (ghostItem) {
                    drawTitleGhost(theme, *ghostItem, row, availableWidth - 8.0f);
                    cursorY += titleRowHeight(*ghostItem) + gapY;
                }
            } else {
                const ImVec2 tile(start.x + visualColumn * (tileW + gapX), cursorY);
                if (ghostItem) {
                    drawItemGhostSlot(theme, *ghostItem, tile, ImVec2(tileW, tileH));
                }
                ++visualColumn;
                if (visualColumn >= columns) {
                    visualColumn = 0;
                    cursorY += tileH + gapY;
                }
            }
            ++ghostOrdinal;
            continue;
        }
        const LaunchItem& item = (*items)[displayOrder[display]];
        if (!deliveredThisFrame && payloadDraggedIdSet.contains(item.id)) {
            continue;
        }
        if (item.type == LaunchItemType::Title) {
            if (visualColumn != 0) {
                visualColumn = 0;
                cursorY += tileH + gapY;
            }
            applyPendingDropLayoutStart(item, "title-layout");
            drawTitleRow(theme, context, api.itemViewApi, *items, displayOrder[display], ImVec2(start.x, cursorY), availableWidth - 8.0f);
            cursorY += titleRowHeight(item) + gapY;
            continue;
        }
        const ImVec2 tile(start.x + visualColumn * (tileW + gapX), cursorY);
        const ImVec2 visualTile = rememberedItemDisplayPosition(item, tile);
        if (!listLocked && item.type == LaunchItemType::VirtualFolder && !payloadDraggedIds.empty() &&
            std::find(payloadDraggedIds.begin(), payloadDraggedIds.end(), item.id) == payloadDraggedIds.end()) {
            const std::string targetKey = "folder-item:" + item.id;
            const ImRect tileRect(visualTile, ImVec2(visualTile.x + tileW, visualTile.y + tileH));
            if (isFolderHoveredForAutoEnter(item, visualTile, tileW, tileH)) {
                if (api.triggerAfterDragHover) {
                    api.triggerAfterDragHover(targetKey, [&]() {
                        if (api.itemViewApi.enterVirtualFolder) {
                            api.itemViewApi.enterVirtualFolder(context, item);
                        }
                    });
                }
                if (api.dragHoverPending && api.dragHoverPending(targetKey)) {
                    ImGui::GetWindowDrawList()->AddRect(tileRect.Min, tileRect.Max, themeColor(theme.frameActive), 6.0f, 0, 1.5f);
                }
            }
        }
        if (tileMode) {
            applyPendingDropLayoutStart(item, "row-layout");
            drawListItem(theme, context, api.itemViewApi, *items, displayOrder[display], tile, tileW, tileH, std::min(iconSize, 36.0f));
        } else {
            applyPendingDropLayoutStart(item, "tile-layout");
            drawIconTile(theme, context, api.itemViewApi, *items, displayOrder[display], tile, ImVec2(tileW, tileH), iconSize);
        }
        if (!listLocked && !deliveredThisFrame && item.type == LaunchItemType::VirtualFolder && !payloadDraggedIds.empty() &&
            acceptDropIntoFolder(context, state, api, folderDropZone(visualTile, tileW, tileH), item, payloadDraggedIds)) {
            deliveredThisFrame = true;
            break;
        }
        ++visualColumn;
        if (visualColumn >= columns) {
            visualColumn = 0;
            cursorY += tileH + gapY;
        }
    }
    if (!deliveredThisFrame && ghostInsert >= 0) {
        const ImRect dropRect(start, ImVec2(start.x + availableWidth - 8.0f, canvasOrigin.y + size.y));
        if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("grid-reorder-drop-zone"))) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(drag_payload::kItemId, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->IsDelivery() && api.dragPayloadId) {
                    deliveredThisFrame = moveDragIdsToList(context, api, activeOrReleasedDragIds(api, context), *items, ghostInsert);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    if (!deliveredThisFrame && !payloadDraggedIds.empty()) {
        if (state.folderStack && !state.folderStack->empty()) {
            if (!dragIdsBelongToList(*items, payloadDraggedIds)) {
                deliveredThisFrame =
                    acceptAppendDrop(context, api, *items, ImRect(canvasOrigin, ImVec2(canvasOrigin.x + size.x, canvasOrigin.y + size.y)),
                                     "grid-current-folder-drop-zone");
            }
            if (!deliveredThisFrame) {
                deliveredThisFrame = fallbackDropIntoCurrentFolder(
                    context, state, api, *items, ImRect(start, ImVec2(start.x + availableWidth - 8.0f, canvasOrigin.y + size.y)));
            }
        }
        if (!deliveredThisFrame) {
            if (!dragIdsBelongToList(*items, payloadDraggedIds)) {
                deliveredThisFrame =
                    acceptAppendDrop(context, api, *items, ImRect(start, ImVec2(start.x + availableWidth - 8.0f, canvasOrigin.y + size.y)),
                                     "grid-append-drop-zone");
            }
        }
    }

    if (visualColumn > 0) {
        cursorY += tileH + gapY;
    }
    if (openBlankMenu && !ImGui::IsAnyItemHovered()) {
        ImGui::OpenPopup("content-menu");
    }
    if (api.drawContentMenu && api.contentMenuState) {
        api.drawContentMenu(theme, context, api.contentMenuState(), api.contentMenuApi);
    }
    if (!deliveredThisFrame && !draggedPreviewItems.empty()) {
        drawDraggedItemPreview(theme, api.itemViewApi, draggedPreviewItems, viewMode, iconSize, tileW, tileH, tileH);
    }
    ImGui::SetCursorScreenPos(ImVec2(start.x, cursorY + 8.0f));
    ImGui::Dummy(ImVec2(1.0f, 1.0f));
    // Drop notes from the notes window onto the main list (supports multi-viewport drag end).
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const ImGuiPayload* payload = ImGui::GetDragDropPayload();
        std::vector<std::string> noteIds = activeDragNoteIdsForDrop();
        if (!noteIds.empty() && (!payload || payload->IsDataType(drag_payload::kNoteId))) {
            addNoteIdsAsListItems(context, noteIds);
        } else if (payload && payload->IsDataType(drag_payload::kNoteId) && payload->IsDelivery()) {
            noteIds = activeDragNoteIdsForDrop();
            if (noteIds.empty() && payload->Data) {
                noteIds.emplace_back(static_cast<const char*>(payload->Data));
            }
            addNoteIdsAsListItems(context, noteIds);
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void captureActiveDragPreviewForDrop(const AppContext& context)
{
    if (gActiveDraggedPreviewItems.empty()) {
        return;
    }
    if (!gActiveDraggedCurrentPreviewPositions.empty()) {
        storePendingDropPreviewPositionsFromActiveSession();
        return;
    }
    const Category* category = selectedCategory(context);
    const ItemViewMode viewMode = (category && !category->useGlobalLayout) ? category->viewMode : context.persisted().settings.viewMode;
    std::vector<const LaunchItem*> items;
    items.reserve(gActiveDraggedPreviewItems.size());
    for (const LaunchItem& item : gActiveDraggedPreviewItems) {
        items.push_back(&item);
    }
    storePendingDropPreviewPositions(items, viewMode);
}

void rememberDragSourceVisualPosition(const LaunchItem& item, const ImVec2& pos)
{
    rememberItemDisplayPosition(item, pos);
}

void clearMainDockDragVisualState()
{
    gPendingDropPreviewPositions.clear();
    gActiveDraggedIds.clear();
    gReleasedDraggedIds.clear();
    gReleasedDraggedFrame = -1;
    gActiveDraggedPreviewItems.clear();
    gActiveDraggedPreviewAnchor.reset();
    gActiveDraggedCurrentPreviewPositions.clear();
}

} // namespace launcher
