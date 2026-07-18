#include "ui/dock/MainDockCategoryRail.hpp"

#include "app/AppContext.hpp"
#include "ui/common/Localization.hpp"
#include "ui/common/UiChrome.hpp"
#include "ui/dock/MainDockDragPayload.hpp"
#include "ui/dock/MainDockGrid.hpp"
#include "ui/dock/MainDockMenu.hpp"
#include "ui/common/MaterialIconRegistry.hpp"
#include "ui/common/MaterialIcons.hpp"
#include "ui/common/UiAnimation.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>

namespace launcher {
namespace {

std::string lower(std::string value)
{
    for (char& ch : value) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return value;
}

bool iconMatchesFilter(const MaterialIconInfo& icon, const std::string& filter)
{
    if (filter.empty()) {
        return true;
    }
    return icon.lowerName.find(filter) != std::string::npos;
}

constexpr float kCategoryRowHeight = 45.0f;
constexpr int kCreateCategoryIndex = -2;
constexpr float kCategoryIconListHeight = 242.0f;
constexpr float kCategoryIconCellMinWidth = 72.0f;
constexpr float kCategoryIconCellHeight = 58.0f;
constexpr float kCategoryIconGridGapX = 6.0f;
constexpr float kCategoryIconGridGapY = 8.0f;
constexpr float kCategoryIconGridPadX = 10.0f;
constexpr float kCategoryIconGridPadY = 8.0f;

ImVec4 colorVecFromHex(const std::string& value, ImVec4 fallback)
{
    if (value.size() != 9 || value[0] != '#') {
        return fallback;
    }
    unsigned int rgba = 0;
    if (std::sscanf(value.c_str() + 1, "%08x", &rgba) != 1) {
        return fallback;
    }
    return ImVec4(((rgba >> 24) & 0xff) / 255.0f, ((rgba >> 16) & 0xff) / 255.0f, ((rgba >> 8) & 0xff) / 255.0f, (rgba & 0xff) / 255.0f);
}

ImU32 categoryIconColor(const UiPalette& theme, const Category& category)
{
    return ImGui::ColorConvertFloat4ToU32(colorVecFromHex(category.iconColor, ImGui::ColorConvertU32ToFloat4(theme.text)));
}

int activeCategoryDragIndex()
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (!payload || !payload->IsDataType(drag_payload::kCategoryIndex) || payload->DataSize != sizeof(int)) {
        return -1;
    }
    return *static_cast<const int*>(payload->Data);
}

int categoryVisibleInsertIndex(int count, int draggedIndex, const ImVec2& start, float railWidth, float height)
{
    if (draggedIndex < 0 || draggedIndex >= count) {
        return -1;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (mouse.x < start.x - 8.0f || mouse.x > start.x + railWidth + 8.0f || mouse.y < start.y - 8.0f || mouse.y > start.y + height + 8.0f) {
        return -1;
    }
    const int visibleCount = std::max(0, count - 1);
    const int row = static_cast<int>(std::floor((mouse.y - start.y) / std::max(1.0f, kCategoryRowHeight)));
    return std::clamp(row, 0, visibleCount);
}

std::vector<int> categoryOrderWithGhost(int count, int draggedIndex, int visibleInsert)
{
    std::vector<int> order;
    order.reserve(static_cast<std::size_t>(count + 1));
    int visible = 0;
    for (int i = 0; i < count; ++i) {
        if (i == draggedIndex) {
            continue;
        }
        if (visible == visibleInsert) {
            order.push_back(-1);
        }
        order.push_back(i);
        ++visible;
    }
    if (visible == visibleInsert) {
        order.push_back(-1);
    }
    return order;
}

int rawCategoryInsertForVisible(int from, int visibleInsert)
{
    return visibleInsert + (from < visibleInsert ? 1 : 0);
}

void drawCategoryGhost(const UiPalette& theme, const Category& category, const ImVec2& pos, float railWidth)
{
    const float alpha = ui_anim::ghostAmount(ImGui::GetID("category-ghost"));
    ImVec4 fill = theme.headerHovered;
    fill.w *= 0.34f * alpha;
    ImVec4 border = theme.frameActive;
    border.w *= alpha;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + railWidth, pos.y + kCategoryRowHeight), ImGui::ColorConvertFloat4ToU32(fill),
                      theme.categoryRounding);
    dl->AddRect(pos, ImVec2(pos.x + railWidth, pos.y + kCategoryRowHeight), ImGui::ColorConvertFloat4ToU32(border), theme.categoryRounding,
                0, 1.4f);
    const char* glyph = materialIconGlyph(category.iconName);
    if (glyph && glyph[0] != '\0') {
        constexpr float categoryIconFontSize = 22.0f;
        const ImVec2 iconSize = ImGui::GetFont()->CalcTextSizeA(categoryIconFontSize, FLT_MAX, 0.0f, glyph);
        ImVec4 iconColor = ImGui::ColorConvertU32ToFloat4(categoryIconColor(theme, category));
        iconColor.w *= alpha;
        dl->AddText(nullptr, categoryIconFontSize, ImVec2(pos.x + 14.0f, pos.y + (kCategoryRowHeight - iconSize.y) * 0.5f),
                    ImGui::ColorConvertFloat4ToU32(iconColor), glyph);
    }
    ImVec4 text = ImGui::ColorConvertU32ToFloat4(theme.text);
    text.w *= alpha;
    dl->AddText(ImVec2(pos.x + 42.0f, pos.y + 13.0f), ImGui::ColorConvertFloat4ToU32(text), category.name.c_str());
}

void drawCategoryDragPreview(const UiPalette& theme, const Category& category)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    constexpr ImVec2 size(148.0f, 42.0f);
    ImGui::Dummy(size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::ColorConvertFloat4ToU32(theme.headerHovered),
                      theme.categoryRounding);
    dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::ColorConvertFloat4ToU32(theme.frameActive), theme.categoryRounding, 0,
                1.2f);
    const char* glyph = materialIconGlyph(category.iconName);
    float textX = pos.x + 14.0f;
    if (glyph && glyph[0] != '\0') {
        constexpr float iconFontSize = 21.0f;
        const ImVec2 iconSize = ImGui::GetFont()->CalcTextSizeA(iconFontSize, FLT_MAX, 0.0f, glyph);
        dl->AddText(nullptr, iconFontSize, ImVec2(pos.x + 12.0f, pos.y + (size.y - iconSize.y) * 0.5f), categoryIconColor(theme, category),
                    glyph);
        textX = pos.x + 40.0f;
    }
    dl->AddText(ImVec2(textX, pos.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f), theme.text, category.name.c_str());
}

ImGuiID categoryLayoutId(const Category& category)
{
    const std::string key = "category-layout:" + category.id;
    return ImHashStr(key.c_str());
}

std::string newCategoryId(int index)
{
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return "category-" + std::to_string(index + 1) + "-" + std::to_string(now);
}

void beginCategoryEdit(AppContext& context, CategoryRailState state, int index)
{
    *state.editingCategoryIndex = index;
    const std::vector<Category>& categories = context.persisted().categories;
    if (index >= 0 && index < static_cast<int>(categories.size())) {
        const Category& category = categories[index];
        *state.editingCategoryName = category.name;
        *state.editingCategoryIconName = category.iconName;
        *state.editingCategoryIconColor = category.iconColor;
    } else {
        *state.editingCategoryName = trs("New Category");
        state.editingCategoryIconName->clear();
        *state.editingCategoryIconColor = "#FFFFFFFF";
    }
    state.categoryIconFilter->clear();
    *state.openCategoryEditorPopup = true;
}

} // namespace

void drawCategoryRail(const UiPalette& theme, AppContext& context, CategoryRailState state, const CategoryRailApi& api,
                      const ImVec2& origin, float height, float railWidth)
{
    if (!state.folderStack || !state.openCategoryEditorPopup || !state.editingCategoryIndex || !state.editingCategoryName ||
        !state.editingCategoryIconName || !state.editingCategoryIconColor || !state.categoryIconFilter) {
        return;
    }

    PersistedState& persisted = context.persisted();
    const AppSettings& settings = persisted.settings;
    RuntimeState& runtime = context.runtime();
    std::vector<Category>& categories = persisted.categories;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec4 sidebar = ImGui::ColorConvertU32ToFloat4(theme.sidebar);
    if (context.themes.active().background.enabled && !context.themes.active().background.imagePath.empty()) {
        sidebar.w *= 0.72f;
    }
    constexpr float kCategoryItemInsetX = 7.0f;
    constexpr float kCategoryItemInsetY = 3.0f;
    const ImU32 sidebarColor = ImGui::ColorConvertFloat4ToU32(sidebar);
    dl->AddRectFilled(origin, ImVec2(origin.x + railWidth, origin.y + height), sidebarColor, theme.sidebarRounding);

    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("categories", ImVec2(railWidth, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    ImGui::Dummy(ImVec2(1.0f, 4.0f));
    const ImVec2 listStart = ImGui::GetCursorScreenPos();
    const int categoryCount = static_cast<int>(categories.size());
    const int draggedCategory = !settings.lockItemLayout ? activeCategoryDragIndex() : -1;
    const int visibleInsert = categoryVisibleInsertIndex(categoryCount, draggedCategory, listStart, railWidth, height);
    const std::vector<int> displayOrder =
        visibleInsert >= 0 ? categoryOrderWithGhost(categoryCount, draggedCategory, visibleInsert) : [&]() {
            std::vector<int> order(categoryCount);
            for (int i = 0; i < categoryCount; ++i) {
                order[i] = i;
            }
            return order;
        }();

    for (int display = 0; display < static_cast<int>(displayOrder.size()); ++display) {
        const ImVec2 targetRow(listStart.x, listStart.y + display * kCategoryRowHeight);
        if (displayOrder[display] < 0) {
            if (draggedCategory >= 0 && draggedCategory < categoryCount) {
                ui_anim::layoutPos(categoryLayoutId(categories[draggedCategory]), targetRow, 0.16f);
                drawCategoryGhost(theme, categories[draggedCategory],
                                  ImVec2(targetRow.x + kCategoryItemInsetX, targetRow.y + kCategoryItemInsetY),
                                  railWidth - kCategoryItemInsetX * 2.0f);
            }
            continue;
        }

        const int i = displayOrder[display];
        ImGui::PushID(i);
        const bool selected = i == runtime.selectedCategory;
        const ImGuiID layoutId = categoryLayoutId(categories[i]);
        const ImVec2 row = ui_anim::layoutPos(layoutId, targetRow, 0.16f);
        const ImRect visualRow(ImVec2(row.x + kCategoryItemInsetX, row.y + kCategoryItemInsetY),
                               ImVec2(row.x + railWidth - kCategoryItemInsetX, row.y + kCategoryRowHeight - kCategoryItemInsetY));
        ImGui::SetCursorScreenPos(row);
        if (ImGui::InvisibleButton("category", ImVec2(railWidth, kCategoryRowHeight))) {
            runtime.selectedCategory = i;
            state.folderStack->clear();
            if (api.clearSelection) {
                api.clearSelection(context);
            }
        }
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("category-menu");
        }
        const ImRect categoryRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (!settings.lockItemLayout && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
            ImGui::SetDragDropPayload(drag_payload::kCategoryIndex, &i, sizeof(i));
            drawCategoryDragPreview(theme, categories[i]);
            ImGui::EndDragDropSource();
        }
        if (!settings.lockItemLayout && ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(drag_payload::kItemId, ImGuiDragDropFlags_AcceptPeekOnly)) {
                const std::string hoverKey = "category:" + categories[i].id;
                dl->AddRectFilled(visualRow.Min, visualRow.Max, theme.sidebarHover, theme.categoryRounding);
                if (api.dragHoverPending && api.dragHoverPending(hoverKey)) {
                    dl->AddRect(visualRow.Min, visualRow.Max, ImGui::ColorConvertFloat4ToU32(theme.frameActive), theme.categoryRounding, 0,
                                1.5f);
                }
                if (api.triggerAfterDragHover) {
                    api.triggerAfterDragHover(hoverKey, [&]() {
                        runtime.selectedCategory = i;
                        state.folderStack->clear();
                        if (api.clearSelection) {
                            api.clearSelection(context);
                        }
                    });
                }
                if (payload->IsDelivery() && api.dragPayloadId && api.moveItemByIdToList) {
                    const std::string primaryId = api.dragPayloadId(payload);
                    captureActiveDragPreviewForDrop(context);
                    const std::vector<std::string> dragIds =
                        api.dragItemIds ? api.dragItemIds(context, primaryId) : std::vector<std::string>{primaryId};
                    if (dragIds.size() > 1 && api.moveItemIdsToList) {
                        api.moveItemIdsToList(context, dragIds, categories[i].items);
                    } else {
                        api.moveItemByIdToList(context, primaryId, categories[i].items);
                    }
                    runtime.selectedCategory = i;
                    state.folderStack->clear();
                }
            }
            ImGui::EndDragDropTarget();
        }
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (hovered && settings.middleClickRunsCategory && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            if (api.runItemsInList) {
                api.runItemsInList(context, categories[i].items);
            }
            if (settings.hideAfterRun && api.requestHideMainWindow) {
                api.requestHideMainWindow();
            }
        }
        if (selected || hovered || active) {
            const ImU32 fill = selected || active ? theme.sidebarActive : theme.sidebarHover;
            dl->AddRectFilled(visualRow.Min, visualRow.Max, fill, theme.categoryRounding);
        }
        ui_anim::rippleLastItemInRect(theme, visualRow.Min, visualRow.Max, theme.categoryRounding);
        const char* glyph = materialIconGlyph(categories[i].iconName);
        if (glyph && glyph[0] != '\0') {
            constexpr float categoryIconFontSize = 22.0f;
            const ImVec2 iconSize = ImGui::GetFont()->CalcTextSizeA(categoryIconFontSize, FLT_MAX, 0.0f, glyph);
            dl->AddText(nullptr, categoryIconFontSize, ImVec2(row.x + 14.0f, row.y + (kCategoryRowHeight - iconSize.y) * 0.5f),
                        categoryIconColor(theme, categories[i]), glyph);
        }
        dl->AddText(ImVec2(row.x + 42.0f, row.y + 13.0f), theme.text, categories[i].name.c_str());

        LightPopupStyle popupStyle(theme);
        const bool animatedPopup = ui_anim::pushPopupAppear("category-menu");
        if (ImGui::BeginPopup("category-menu")) {
            suppressCurrentViewportNativeBorder();
            if (menuItem(theme, Icons::Add, tr("Add Item"), "Ctrl+N") && api.openItemEditor) {
                runtime.selectedCategory = i;
                api.openItemEditor(context, -1);
            }
            if (menuItem(theme, Icons::Edit, tr("Edit"), "F2")) {
                beginCategoryEdit(context, state, i);
            }
            if (menuItem(theme, Icons::Delete, tr("Delete"), "Del", false, categories.size() > 1) && api.requestDeleteCategory) {
                api.requestDeleteCategory(context, i);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (animatedPopup) {
            ui_anim::popAppearAlpha();
        }
        ImGui::PopID();
    }
    const float listEndY = listStart.y + displayOrder.size() * kCategoryRowHeight;
    const float blankHeight = (origin.y + height) - listEndY;
    ImGui::SetCursorScreenPos(ImVec2(listStart.x, listEndY));
    if (blankHeight > 0.5f) {
        ImGui::InvisibleButton("category-blank-area", ImVec2(railWidth, blankHeight));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("category-blank-menu");
        }
    } else {
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
    }
    {
        LightPopupStyle popupStyle(theme);
        const bool animatedPopup = ui_anim::pushPopupAppear("category-blank-menu");
        if (ImGui::BeginPopup("category-blank-menu")) {
            suppressCurrentViewportNativeBorder();
            if (menuItem(theme, Icons::Add, tr("New Category"), "Ctrl+N")) {
                beginCategoryEdit(context, state, kCreateCategoryIndex);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if (animatedPopup) {
            ui_anim::popAppearAlpha();
        }
    }
    if (visibleInsert >= 0 && api.reorderCategory) {
        const ImRect dropRect(listStart, ImVec2(listStart.x + railWidth, listStart.y + height));
        if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("category-reorder-drop-zone"))) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(drag_payload::kCategoryIndex, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
                if (payload->IsDelivery()) {
                    const int from = *static_cast<const int*>(payload->Data);
                    api.reorderCategory(context, from, rawCategoryInsertForVisible(from, visibleInsert));
                }
            }
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    if (*state.editingCategoryIndex >= 0 || *state.editingCategoryIndex == kCreateCategoryIndex) {
        setupManagedWindow("LauncherManagedCategoryEditor");

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(460.0f, 540.0f), ImGuiCond_Appearing);
        if (*state.openCategoryEditorPopup) {
            ImGui::SetNextWindowFocus();
            *state.openCategoryEditorPopup = false;
        }
        ManagedWindowStyle windowStyle(theme);
        bool open = true;
        if (ImGui::Begin("Edit Category###category-editor", &open,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
            applyManagedViewportChrome(ImGui::GetWindowViewport()->PlatformHandleRaw, context.themes.active(), theme);
            drawManagedTitleBar(theme, tr("Edit Category"), open);
            if (!open) {
                *state.editingCategoryIndex = -1;
                ImGui::End();
                return;
            }
            ImGui::SetCursorPos(ImVec2(12.0f, kUiTitleHeight + 10.0f));
            ImGui::BeginChild("category-edit-scroll", ImVec2(-12.0f, -58.0f), ImGuiChildFlags_None);
            ImGui::TextUnformatted(tr("Name"));
            ImGui::InputText("##category-name", state.editingCategoryName);
            ImGui::Spacing();
            ImGui::TextUnformatted(tr("Icon"));
            const char* currentGlyph = materialIconGlyph(*state.editingCategoryIconName);
            std::string preview = (currentGlyph && currentGlyph[0] != '\0')
                                      ? std::string(currentGlyph) + "  " + *state.editingCategoryIconName
                                      : trs("Not set");
            ImVec4 iconColor = colorVecFromHex(*state.editingCategoryIconColor, ImGui::ColorConvertU32ToFloat4(theme.text));
            ImGui::Button(preview.c_str(), ImVec2(-116.0f, 32.0f));
            ImGui::SameLine();
            if (ImGui::ColorButton("##category-icon-color-preview", iconColor, ImGuiColorEditFlags_AlphaPreviewHalf,
                                   ImVec2(36.0f, 32.0f))) {
                ImGui::OpenPopup("category-icon-color-picker");
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Clear"), ImVec2(62.0f, 32.0f))) {
                state.editingCategoryIconName->clear();
            }
            {
                LightPopupStyle colorPopupStyle(theme);
                const bool colorPopupAnimated = ui_anim::pushPopupAppear("category-icon-color-picker");
                if (ImGui::BeginPopup("category-icon-color-picker")) {
                    suppressCurrentViewportNativeBorder();
                    if (ImGui::ColorPicker4("##category-icon-color", &iconColor.x,
                                            ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf |
                                                ImGuiColorEditFlags_DisplayRGB)) {
                        *state.editingCategoryIconColor = colorHexFromVec(iconColor);
                    }
                    ImGui::EndPopup();
                }
                if (colorPopupAnimated) {
                    ui_anim::popAppearAlpha();
                }
            }
            ImGui::Spacing();
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##category-icon-filter", tr("Search icon"), state.categoryIconFilter);
            ImGui::BeginChild("category-icon-list", ImVec2(-1.0f, kCategoryIconListHeight), ImGuiChildFlags_Borders);
            const std::string filter = lower(*state.categoryIconFilter);
            const auto& icons = materialIconRegistry();
            std::vector<int> filtered;
            filtered.reserve(icons.size());
            for (int idx = 0; idx < static_cast<int>(icons.size()); ++idx) {
                if (iconMatchesFilter(icons[idx], filter)) {
                    filtered.push_back(idx);
                }
            }
            if (filtered.empty()) {
                ImGui::SetCursorPos(ImVec2(kCategoryIconGridPadX, kCategoryIconGridPadY));
                ImGui::TextDisabled("%s", tr("No results"));
            } else {
                const float contentWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
                const float usableWidth = std::max(kCategoryIconCellMinWidth, contentWidth - kCategoryIconGridPadX * 2.0f);
                const int columns = std::max(
                    1, static_cast<int>((usableWidth + kCategoryIconGridGapX) / (kCategoryIconCellMinWidth + kCategoryIconGridGapX)));
                const float cellW = std::floor((usableWidth - kCategoryIconGridGapX * std::max(0, columns - 1)) / columns);
                const int rows = static_cast<int>((filtered.size() + columns - 1) / columns);
                const float rowStride = kCategoryIconCellHeight + kCategoryIconGridGapY;
                const ImVec2 gridStart(ImGui::GetCursorPosX() + kCategoryIconGridPadX, ImGui::GetCursorPosY() + kCategoryIconGridPadY);
                ImGuiListClipper clipper;
                clipper.Begin(rows, rowStride);
                while (clipper.Step()) {
                    for (int rowIdx = clipper.DisplayStart; rowIdx < clipper.DisplayEnd; ++rowIdx) {
                        for (int col = 0; col < columns; ++col) {
                            const int at = rowIdx * columns + col;
                            if (at >= static_cast<int>(filtered.size())) {
                                break;
                            }
                            const MaterialIconInfo& icon = icons[filtered[at]];
                            const bool selected = *state.editingCategoryIconName == icon.name;
                            ImGui::PushID(icon.name.c_str());
                            ImGui::SetCursorPos(
                                ImVec2(gridStart.x + col * (cellW + kCategoryIconGridGapX), gridStart.y + rowIdx * rowStride));
                            ImGui::InvisibleButton("icon-cell", ImVec2(cellW, kCategoryIconCellHeight));
                            const ImVec2 cellPos = ImGui::GetItemRectMin();
                            const bool hovered = ImGui::IsItemHovered();
                            const ImU32 bg = ImGui::ColorConvertFloat4ToU32(selected  ? theme.headerActive
                                                                            : hovered ? theme.headerHovered
                                                                                      : theme.childBg);
                            const ImU32 border = ImGui::ColorConvertFloat4ToU32(selected ? theme.frameActive : theme.border);
                            ImDrawList* list = ImGui::GetWindowDrawList();
                            list->AddRectFilled(cellPos, ImVec2(cellPos.x + cellW, cellPos.y + kCategoryIconCellHeight), bg,
                                                theme.categoryRounding);
                            list->AddRect(cellPos, ImVec2(cellPos.x + cellW, cellPos.y + kCategoryIconCellHeight), border,
                                          theme.categoryRounding);
                            constexpr float iconFontSize = 28.0f;
                            const ImVec2 iconSize = ImGui::GetFont()->CalcTextSizeA(iconFontSize, FLT_MAX, 0.0f, icon.utf8.c_str());
                            list->AddText(nullptr, iconFontSize, ImVec2(cellPos.x + (cellW - iconSize.x) * 0.5f, cellPos.y + 5.0f),
                                          theme.text, icon.utf8.c_str());
                            const float textMaxW = cellW - 8.0f;
                            std::string clipped = icon.name;
                            while (!clipped.empty() && ImGui::CalcTextSize(clipped.c_str()).x > textMaxW) {
                                clipped.pop_back();
                            }
                            if (clipped.size() < icon.name.size() && clipped.size() > 1) {
                                clipped.resize(std::max<std::size_t>(1, clipped.size() - 1));
                                clipped += "...";
                            }
                            const ImVec2 textSize = ImGui::CalcTextSize(clipped.c_str());
                            list->AddText(ImVec2(cellPos.x + std::max(4.0f, (cellW - textSize.x) * 0.5f), cellPos.y + 33.0f), theme.text,
                                          clipped.c_str());
                            if (ImGui::IsItemClicked()) {
                                *state.editingCategoryIconName = icon.name;
                            }
                            ImGui::PopID();
                        }
                    }
                }
                ImGui::SetCursorPos(ImVec2(0.0f, gridStart.y + std::max(0.0f, rows * rowStride - kCategoryIconGridGapY)));
                ImGui::Dummy(ImVec2(contentWidth, 1.0f));
            }
            ImGui::EndChild();
            ImGui::EndChild();

            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 194.0f);
            if (ImGui::Button(tr("OK"), ImVec2(86.0f, 30.0f))) {
                if (!state.editingCategoryName->empty()) {
                    if (*state.editingCategoryIndex >= 0 && *state.editingCategoryIndex < static_cast<int>(categories.size())) {
                        categories[*state.editingCategoryIndex].name = *state.editingCategoryName;
                        categories[*state.editingCategoryIndex].iconName = *state.editingCategoryIconName;
                        categories[*state.editingCategoryIndex].iconColor = *state.editingCategoryIconColor;
                    } else if (*state.editingCategoryIndex == kCreateCategoryIndex) {
                        Category category;
                        category.id = newCategoryId(static_cast<int>(categories.size()));
                        category.name = *state.editingCategoryName;
                        category.iconName = *state.editingCategoryIconName;
                        category.iconColor = state.editingCategoryIconColor->empty() ? "#FFFFFFFF" : *state.editingCategoryIconColor;
                        category.useGlobalLayout = true;
                        category.viewMode = settings.viewMode;
                        category.iconSize = settings.iconSize;
                        category.nameLines = settings.nameLines;
                        categories.push_back(std::move(category));
                        runtime.selectedCategory = static_cast<int>(categories.size()) - 1;
                        state.folderStack->clear();
                        if (api.clearSelection) {
                            api.clearSelection(context);
                        }
                    }
                    context.commitContentChange();
                }
                *state.editingCategoryIndex = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"), ImVec2(86.0f, 30.0f))) {
                *state.editingCategoryIndex = -1;
            }
            if (!open) {
                *state.editingCategoryIndex = -1;
            }
        }
        ImGui::End();
    }
}

} // namespace launcher
