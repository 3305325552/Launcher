#include "ui/MainDockItemViews.hpp"

#include "app/AppContext.hpp"
#include "ui/MainDockDragPayload.hpp"
#include "ui/MainDockGrid.hpp"
#include "ui/MainDockState.hpp"
#include "ui/UiAnimation.hpp"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace launcher {
namespace {

ImU32 themeColor(const ImVec4& color)
{
    return ImGui::ColorConvertFloat4ToU32(color);
}

ImU32 itemHighlightColor(const UiPalette& theme, bool selected)
{
    return themeColor(selected ? theme.headerActive : theme.headerHovered);
}

ImU32 itemHighlightColor(const UiPalette& theme, bool selected, float hoverAmount)
{
    if (selected) {
        return themeColor(theme.headerActive);
    }
    ImVec4 color = theme.headerHovered;
    color.w *= hoverAmount;
    return themeColor(color);
}

ImU32 itemBorderColor(const UiPalette& theme, bool selected)
{
    return themeColor(selected ? theme.frameActive : theme.border);
}

bool isFolderHoveredForAutoEnter(const LaunchItem& item, const ImVec2& tile, float tileW, float tileH)
{
    if (item.type != LaunchItemType::VirtualFolder) {
        return false;
    }
    ImRect centerZone(ImVec2(tile.x + tileW * 0.28f, tile.y + tileH * 0.24f), ImVec2(tile.x + tileW * 0.72f, tile.y + tileH * 0.76f));
    if (tileW > tileH * 2.0f) {
        centerZone.Min.x = tile.x;
        centerZone.Max.x = std::max(centerZone.Max.x, tile.x + std::min(tileW, 68.0f));
    }
    return centerZone.Contains(ImGui::GetIO().MousePos);
}

bool activateItemFromPrimaryClick(AppContext& context, LaunchItem& item, const ItemViewApi& api)
{
    const ImGuiIO& io = ImGui::GetIO();
    if (context.persisted().settings.doubleClickRun || io.KeyCtrl || io.KeyShift) {
        return false;
    }
    if (item.type == LaunchItemType::VirtualFolder) {
        if (api.enterVirtualFolder) {
            api.enterVirtualFolder(context, item);
        }
        return true;
    }
    if (item.type == LaunchItemType::Title || item.type == LaunchItemType::Placeholder) {
        return false;
    }
    if (api.runItem) {
        api.runItem(context, item, SW_SHOWNORMAL);
    }
    return true;
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

std::string elideText(const char* text, float maxWidth)
{
    if (!text || maxWidth <= 0.0f) {
        return {};
    }
    if (ImGui::CalcTextSize(text).x <= maxWidth) {
        return text;
    }
    std::string value(text);
    constexpr const char* ellipsis = "...";
    const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
    while (!value.empty() && ImGui::CalcTextSize(value.c_str()).x + ellipsisWidth > maxWidth) {
        if ((static_cast<unsigned char>(value.back()) & 0x80) == 0) {
            value.pop_back();
        } else {
            while (!value.empty() && (static_cast<unsigned char>(value.back()) & 0xC0) == 0x80) {
                value.pop_back();
            }
            if (!value.empty()) {
                value.pop_back();
            }
        }
    }
    if (value.empty()) {
        return ellipsis;
    }
    value += ellipsis;
    return value;
}

std::string elideTextForced(const std::string& text, float maxWidth)
{
    if (text.empty() || maxWidth <= 0.0f) {
        return {};
    }
    constexpr const char* ellipsis = "...";
    const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
    if (maxWidth <= ellipsisWidth) {
        return ellipsis;
    }
    std::string value = text;
    while (!value.empty() && ImGui::CalcTextSize(value.c_str()).x + ellipsisWidth > maxWidth) {
        if ((static_cast<unsigned char>(value.back()) & 0x80) == 0) {
            value.pop_back();
        } else {
            while (!value.empty() && (static_cast<unsigned char>(value.back()) & 0xC0) == 0x80) {
                value.pop_back();
            }
            if (!value.empty()) {
                value.pop_back();
            }
        }
    }
    if (value.empty()) {
        return ellipsis;
    }
    value += ellipsis;
    return value;
}

size_t utf8CharLength(unsigned char ch)
{
    if ((ch & 0x80) == 0) {
        return 1;
    }
    if ((ch & 0xE0) == 0xC0) {
        return 2;
    }
    if ((ch & 0xF0) == 0xE0) {
        return 3;
    }
    if ((ch & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

std::vector<std::string> wrapTextLines(const std::string& text, float maxWidth, int maxLines)
{
    std::vector<std::string> lines;
    if (text.empty() || maxWidth <= 0.0f || maxLines <= 0) {
        return lines;
    }

    std::string current;
    size_t pos = 0;
    while (pos < text.size() && static_cast<int>(lines.size()) < maxLines) {
        if (text[pos] == '\r' || text[pos] == '\n') {
            if (!current.empty()) {
                lines.push_back(current);
                current.clear();
            }
            ++pos;
            continue;
        }

        const size_t len = std::min(utf8CharLength(static_cast<unsigned char>(text[pos])), text.size() - pos);
        const std::string next = text.substr(pos, len);
        const std::string candidate = current + next;
        if (current.empty() || ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) {
            current = candidate;
            pos += len;
            continue;
        }

        lines.push_back(current);
        current.clear();
    }
    if (static_cast<int>(lines.size()) < maxLines && !current.empty()) {
        lines.push_back(current);
    }
    if (!lines.empty() && pos < text.size()) {
        lines.back() = elideTextForced(lines.back(), maxWidth);
    }
    return lines;
}

void beginItemDragSource(AppContext& context, const ItemViewApi& api, const LaunchItem& item)
{
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID | ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
        if (api.captureDragItemIds) {
            api.captureDragItemIds(context, item.id);
        }
        ImGui::SetDragDropPayload(drag_payload::kItemId, item.id.c_str(), static_cast<int>(item.id.size() + 1));
        ImGui::EndDragDropSource();
    }
}

} // namespace

float itemViewTitleRowHeight(const LaunchItem& item)
{
    const float fontSize = static_cast<float>(std::clamp(item.titleSize, 12, 48));
    return std::max(42.0f, fontSize + 20.0f);
}

void drawTitleRow(const UiPalette& theme, AppContext& context, const ItemViewApi& api, std::vector<LaunchItem>& items, int itemIndex,
                  const ImVec2& row, float width)
{
    LaunchItem& item = items[itemIndex];
    const float height = itemViewTitleRowHeight(item);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::PushID(item.id.c_str());
    const ImVec2 visualRow = ui_anim::layoutPos(ImGui::GetID("title-layout"), row);
    rememberDragSourceVisualPosition(item, visualRow);
    ImGui::SetCursorScreenPos(visualRow);
    ImGui::InvisibleButton("title-row", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool selected = api.isItemSelected && api.isItemSelected(item);
    const float hoverT = ui_anim::hoverAmount(ImGui::GetID("title-hover"), hovered);
    const float rounding = theme.itemRounding;
    if (hoverT > 0.01f || selected) {
        dl->AddRectFilled(visualRow, ImVec2(visualRow.x + width, visualRow.y + height), itemHighlightColor(theme, selected, hoverT),
                          rounding);
    }
    ui_anim::rippleLastItem(theme, rounding);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && api.handleItemSelectionClick) {
        api.handleItemSelectionClick(context, items, itemIndex);
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (api.selectSingle) {
            api.selectSingle(context, item);
        }
        ImGui::OpenPopup("item-menu");
    }
    const AppSettings& settings = context.persisted().settings;
    if (settings.sortMode == SortMode::Free && !settings.lockItemLayout && !currentListLayoutLocked(context)) {
        beginItemDragSource(context, api, item);
    }

    const float fontSize = static_cast<float>(std::clamp(item.titleSize, 12, 48));
    const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, item.name.c_str());
    float x = visualRow.x + 8.0f;
    if (item.titleAlign == 1) {
        x = visualRow.x + std::max(8.0f, (width - textSize.x) * 0.5f);
    } else if (item.titleAlign == 2) {
        x = visualRow.x + std::max(8.0f, width - textSize.x - 12.0f);
    }
    dl->AddText(nullptr, fontSize, ImVec2(x, visualRow.y + (height - textSize.y) * 0.5f), theme.text, item.name.c_str());
    if (api.drawItemMenu) {
        api.drawItemMenu(theme, context, items, itemIndex);
    }
    ImGui::PopID();
}

void drawIconTile(const UiPalette& theme, AppContext& context, const ItemViewApi& api, std::vector<LaunchItem>& items, int itemIndex,
                  const ImVec2& tile, const ImVec2& tileSize, float iconSize)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    LaunchItem& item = items[itemIndex];
    ImGui::PushID(item.id.c_str());
    const ImVec2 visualTile = ui_anim::layoutPos(ImGui::GetID("tile-layout"), tile);
    rememberDragSourceVisualPosition(item, visualTile);
    ImGui::SetCursorScreenPos(visualTile);
    ImGui::InvisibleButton("tile", tileSize);
    const ImRect itemRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const bool hovered = ImGui::IsItemHovered();
    const AppSettings& settings = context.persisted().settings;
    const bool selected = api.isItemSelected && api.isItemSelected(item);
    const float hoverT = ui_anim::hoverAmount(ImGui::GetID("tile-hover"), hovered);
    const float rounding = theme.itemRounding;
    if (hoverT > 0.01f || selected) {
        dl->AddRectFilled(visualTile, ImVec2(visualTile.x + tileSize.x, visualTile.y + tileSize.y),
                          itemHighlightColor(theme, selected, hoverT), rounding);
        dl->AddRect(visualTile, ImVec2(visualTile.x + tileSize.x, visualTile.y + tileSize.y), itemBorderColor(theme, selected), rounding);
    }
    ui_anim::rippleLastItem(theme, rounding);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !activateItemFromPrimaryClick(context, item, api)) {
        if (api.handleItemSelectionClick) {
            api.handleItemSelectionClick(context, items, itemIndex);
        }
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (settings.shiftRightClickExplorerMenu && ImGui::GetIO().KeyShift && !item.target.empty()) {
            if (api.showFileProperties) {
                api.showFileProperties(item);
            }
            ImGui::PopID();
            return;
        }
        if (api.isItemSelected && !api.isItemSelected(item)) {
            if (api.selectSingle) {
                api.selectSingle(context, item);
            }
        } else {
            context.runtime().selectedItemId = item.id;
        }
        ImGui::OpenPopup("item-menu");
    }
    if (settings.sortMode == SortMode::Free && !settings.lockItemLayout && !currentListLayoutLocked(context)) {
        beginItemDragSource(context, api, item);
    }
    if (hovered && settings.doubleClickRun && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (item.type == LaunchItemType::VirtualFolder) {
            if (api.enterVirtualFolder) {
                api.enterVirtualFolder(context, item);
            }
        } else if (api.runItem) {
            api.runItem(context, item, SW_SHOWNORMAL);
        }
    }
    if (hovered && api.drawItemTooltip) {
        api.drawItemTooltip(theme, settings, item, itemRect);
    }

    if (item.type == LaunchItemType::Title) {
        const std::string title = elideText(item.name.c_str(), tileSize.x - 16.0f);
        dl->AddText(ImVec2(visualTile.x + 8.0f, visualTile.y + 16.0f), theme.text, title.c_str());
    } else {
        const float iconPadding = std::max(6.0f, iconSize * 0.15f);
        const ImVec2 iconPos(visualTile.x + (tileSize.x - iconSize) * 0.5f, visualTile.y + iconPadding);
        if (api.drawLaunchIcon) {
            api.drawLaunchIcon(item, iconPos, iconSize);
        }
        const int nameLines = std::clamp(effectiveNameLines(context), 0, 3);
        if (nameLines > 0 && item.type != LaunchItemType::Placeholder) {
            const float labelMaxWidth = std::max(0.0f, tileSize.x - 8.0f);
            const std::vector<std::string> lines = wrapTextLines(item.name, labelMaxWidth, nameLines);
            const float textY = visualTile.y + iconPadding + iconSize + 6.0f;
            const float lineHeight = ImGui::GetTextLineHeight();
            dl->PushClipRect(ImVec2(visualTile.x + 4.0f, textY), ImVec2(visualTile.x + tileSize.x - 4.0f, visualTile.y + tileSize.y - 4.0f),
                             true);
            for (size_t lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
                const std::string& line = lines[lineIndex];
                const ImVec2 textSize = ImGui::CalcTextSize(line.c_str());
                const float textX = visualTile.x + 4.0f + std::max(0.0f, (labelMaxWidth - textSize.x) * 0.5f);
                dl->AddText(ImVec2(std::floor(textX), textY + lineHeight * static_cast<float>(lineIndex)), theme.text, line.c_str());
            }
            dl->PopClipRect();
        }
    }
    if (api.drawItemMenu) {
        api.drawItemMenu(theme, context, items, itemIndex);
    }
    ImGui::PopID();
}

void drawListItem(const UiPalette& theme, AppContext& context, const ItemViewApi& api, std::vector<LaunchItem>& items, int itemIndex,
                  const ImVec2& row, float width, float height, float iconSize)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    LaunchItem& item = items[itemIndex];
    ImGui::PushID(item.id.c_str());
    const ImVec2 visualRow = ui_anim::layoutPos(ImGui::GetID("row-layout"), row);
    rememberDragSourceVisualPosition(item, visualRow);
    ImGui::SetCursorScreenPos(visualRow);
    ImGui::InvisibleButton("row", ImVec2(width, height));
    const ImRect itemRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    const bool hovered = ImGui::IsItemHovered();
    const AppSettings& settings = context.persisted().settings;
    const bool selected = api.isItemSelected && api.isItemSelected(item);
    const float hoverT = ui_anim::hoverAmount(ImGui::GetID("row-hover"), hovered);
    const float rounding = theme.itemRounding;
    if (hoverT > 0.01f || selected) {
        dl->AddRectFilled(visualRow, ImVec2(visualRow.x + width, visualRow.y + height), itemHighlightColor(theme, selected, hoverT),
                          rounding);
    }
    ui_anim::rippleLastItem(theme, rounding);
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && !activateItemFromPrimaryClick(context, item, api)) {
        if (api.handleItemSelectionClick) {
            api.handleItemSelectionClick(context, items, itemIndex);
        }
    }
    if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
        if (settings.shiftRightClickExplorerMenu && ImGui::GetIO().KeyShift && !item.target.empty()) {
            if (api.showFileProperties) {
                api.showFileProperties(item);
            }
            ImGui::PopID();
            return;
        }
        if (api.isItemSelected && !api.isItemSelected(item)) {
            if (api.selectSingle) {
                api.selectSingle(context, item);
            }
        } else {
            context.runtime().selectedItemId = item.id;
        }
        ImGui::OpenPopup("item-menu");
    }
    if (settings.sortMode == SortMode::Free && !settings.lockItemLayout && !currentListLayoutLocked(context)) {
        beginItemDragSource(context, api, item);
        if (item.type == LaunchItemType::VirtualFolder && isFolderHoveredForAutoEnter(item, visualRow, width, height)) {
            if (const ImGuiPayload* payload = ImGui::GetDragDropPayload(); payload && payload->IsDataType(drag_payload::kItemId)) {
                const std::string id = api.dragPayloadId ? api.dragPayloadId(payload) : std::string{};
                if (!id.empty() && id != item.id) {
                    const std::string targetKey = "folder-item:" + item.id;
                    if (api.triggerAfterDragHover) {
                        api.triggerAfterDragHover(targetKey, [&]() {
                            if (api.enterVirtualFolder) {
                                api.enterVirtualFolder(context, item);
                            }
                        });
                    }
                    if (api.dragHoverPending && api.dragHoverPending(targetKey)) {
                        ImGui::GetWindowDrawList()->AddRect(itemRect.Min, itemRect.Max, themeColor(theme.frameActive), 6.0f, 0, 1.5f);
                    }
                }
            }
        }
    }
    if (hovered && settings.doubleClickRun && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        if (item.type == LaunchItemType::VirtualFolder) {
            if (api.enterVirtualFolder) {
                api.enterVirtualFolder(context, item);
            }
        } else if (api.runItem) {
            api.runItem(context, item, SW_SHOWNORMAL);
        }
    }
    if (hovered && api.drawItemTooltip) {
        api.drawItemTooltip(theme, settings, item, itemRect);
    }

    if (item.type == LaunchItemType::Title) {
        const float fontSize = static_cast<float>(std::clamp(item.titleSize, 12, 48));
        const std::string title = elideText(item.name.c_str(), width - 20.0f);
        const ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, title.c_str());
        float x = visualRow.x + 8.0f;
        if (item.titleAlign == 1) {
            x = visualRow.x + std::max(8.0f, (width - textSize.x) * 0.5f);
        } else if (item.titleAlign == 2) {
            x = visualRow.x + std::max(8.0f, width - textSize.x - 12.0f);
        }
        dl->AddText(nullptr, fontSize, ImVec2(x, visualRow.y + (height - textSize.y) * 0.5f), theme.text, title.c_str());
    } else {
        if (api.drawLaunchIcon) {
            api.drawLaunchIcon(item, ImVec2(visualRow.x + 8.0f, visualRow.y + (height - iconSize) * 0.5f), iconSize);
        }
        const float textX = visualRow.x + 16.0f + iconSize;
        const float textMax = std::max(20.0f, width - (textX - visualRow.x) - 12.0f);
        const float textLineH = ImGui::GetTextLineHeight();
        const bool showSubtitle = !item.subtitle.empty();
        int nameLines = std::clamp(effectiveNameLines(context), 1, 3);
        if (showSubtitle) {
            const int availableLines = std::max(2, static_cast<int>(std::floor(height / textLineH)));
            nameLines = std::min(nameLines, std::max(1, availableLines - 1));
        }
        std::vector<std::string> name = wrapTextLines(item.name, textMax, nameLines);
        if (name.empty()) {
            name.push_back({});
        }
        if (showSubtitle) {
            const float totalLines = static_cast<float>(name.size() + 1);
            const float textTop = visualRow.y + std::max(0.0f, (height - textLineH * totalLines) * 0.5f);
            for (size_t lineIndex = 0; lineIndex < name.size(); ++lineIndex) {
                dl->AddText(ImVec2(textX, textTop + textLineH * static_cast<float>(lineIndex)), theme.text, name[lineIndex].c_str());
            }
            const std::string subtitle = elideText(item.subtitle.c_str(), textMax);
            dl->AddText(ImVec2(textX, textTop + textLineH * static_cast<float>(name.size())), theme.textMuted, subtitle.c_str());
        } else {
            const float textTop = visualRow.y + (height - textLineH * static_cast<float>(std::max<size_t>(1, name.size()))) * 0.5f;
            for (size_t lineIndex = 0; lineIndex < std::max<size_t>(1, name.size()); ++lineIndex) {
                const char* line = lineIndex < name.size() ? name[lineIndex].c_str() : "";
                dl->AddText(ImVec2(textX, textTop + textLineH * static_cast<float>(lineIndex)), theme.text, line);
            }
        }
    }
    if (api.drawItemMenu) {
        api.drawItemMenu(theme, context, items, itemIndex);
    }
    ImGui::PopID();
}

} // namespace launcher
