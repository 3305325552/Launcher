#include "ui/settings/ThemeEditor.hpp"

#include "app/AppContext.hpp"
#include "core/AnimatedBackground.hpp"
#include "core/StringEncoding.hpp"
#include "ui/common/Localization.hpp"
#include "ui/common/UiChrome.hpp"
#include "ui/common/UiAnimation.hpp"
#include "ui/common/UiTheme.hpp"

#include <windows.h>
#include <commdlg.h>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cfloat>
#include <filesystem>
#include <future>
#include <string>
#include <vector>

namespace launcher {
namespace {

struct ThemeRow {
    enum class Type {
        Color,
        Scalar,
    };
    std::string group;
    std::string key;
    std::string label;
    Type type = Type::Color;
    ImVec4 color;
    float scalar = 0.0f;
    float minValue = 0.0f;
    float maxValue = 100.0f;
};

struct BackgroundCacheJobResult {
    bool ok = false;
    std::string message;
};

ImVec4 colorFromU32(ImU32 color)
{
    return ImGui::ColorConvertU32ToFloat4(color);
}

void addColorRow(std::vector<ThemeRow>& rows, const char* group, const char* key, const ImVec4& color, const char* label = nullptr)
{
    ThemeRow row;
    row.group = group;
    row.key = key;
    row.label = label ? label : key;
    row.type = ThemeRow::Type::Color;
    row.color = color;
    rows.push_back(row);
}

void addScalarRow(std::vector<ThemeRow>& rows, const char* group, const char* key, float value, float minValue, float maxValue,
                  const char* label = nullptr)
{
    ThemeRow row;
    row.group = group;
    row.key = key;
    row.label = label ? label : key;
    row.type = ThemeRow::Type::Scalar;
    row.scalar = value;
    row.minValue = minValue;
    row.maxValue = maxValue;
    rows.push_back(row);
}

void overlaySavedRows(std::vector<ThemeRow>& rows, const std::vector<ThemeColor>& saved)
{
    for (const ThemeColor& color : saved) {
        auto it = std::find_if(rows.begin(), rows.end(), [&](const ThemeRow& row) {
            return row.key == color.key;
        });
        if (it == rows.end()) {
            continue;
        }
        if (it->type == ThemeRow::Type::Scalar) {
            it->scalar = color.color[0];
        } else {
            it->color = ImVec4(color.color[0], color.color[1], color.color[2], color.color[3]);
        }
    }
}

std::vector<ThemeRow> makeRows(const ThemeDefinition& theme)
{
    const UiPalette p = uiPalette(theme);
    std::vector<ThemeRow> rows;
    rows.reserve(64);

    addColorRow(rows, "Global", "global_text", colorFromU32(p.text), "Global text");
    addColorRow(rows, "Global", "global_text_disabled", colorFromU32(p.textMuted), "Global text disabled");
    addColorRow(rows, "Global", "global_separator", p.border, "Global separator");
    addColorRow(rows, "Global", "ripple_color", colorFromU32(p.rippleColor), "Ripple color");

    addColorRow(rows, "Window", "content_background", colorFromU32(p.contentBg), "Main background");
    addColorRow(rows, "Window", "window_background", p.windowBg, "Window background");
    addColorRow(rows, "Window", "child_background", p.childBg, "Child background");
    addColorRow(rows, "Window", "popup_background", p.popupBg, "Popup background");
    addColorRow(rows, "Window", "border", p.border, "Border");
    addColorRow(rows, "Window", "window_outline", p.windowOutline, "Window outline");
    addColorRow(rows, "Window", "popup_outline", p.popupOutline, "Popup outline");

    addColorRow(rows, "Title Bar", "top_background", colorFromU32(p.titleBar), "Title bar background");
    addColorRow(rows, "Title Bar", "top_button_hover", colorFromU32(p.titleButtonHover), "Title button hover");
    addColorRow(rows, "Title Bar", "top_button_active", colorFromU32(p.titleButtonActive), "Title button active");
    addColorRow(rows, "Search", "search_background", colorFromU32(p.searchBar), "Search background");

    addColorRow(rows, "Sidebar", "left_background", colorFromU32(p.sidebar), "Sidebar background");
    addColorRow(rows, "Sidebar", "tab_background_hover", colorFromU32(p.sidebarHover), "Sidebar item hover");
    addColorRow(rows, "Sidebar", "tab_background_active", colorFromU32(p.sidebarActive), "Sidebar item active");

    addColorRow(rows, "Controls", "frame_background", p.frameBg, "Frame background");
    addColorRow(rows, "Controls", "frame_background_hover", p.frameHovered, "Frame hover");
    addColorRow(rows, "Controls", "frame_background_active", p.frameActive, "Frame active");
    addColorRow(rows, "Controls", "button_background", p.button, "Button background");
    addColorRow(rows, "Controls", "button_background_hover", p.buttonHovered, "Button hover");
    addColorRow(rows, "Controls", "button_background_active", p.buttonActive, "Button active");
    addColorRow(rows, "Controls", "header_background", p.header, "Header background");
    addColorRow(rows, "Controls", "header_background_hover", p.headerHovered, "Header hover");
    addColorRow(rows, "Controls", "header_background_active", p.headerActive, "Header active");

    addScalarRow(rows, "Rounding", "window_rounding", p.windowRounding, 0.0f, 24.0f, "Window rounding");
    addScalarRow(rows, "Rounding", "popup_rounding", p.popupRounding, 0.0f, 24.0f, "Popup rounding");
    addScalarRow(rows, "Rounding", "frame_rounding", p.frameRounding, 0.0f, 16.0f, "Frame rounding");
    addScalarRow(rows, "Rounding", "item_rounding", p.itemRounding, 0.0f, 20.0f, "Item rounding");
    addScalarRow(rows, "Rounding", "category_rounding", p.categoryRounding, 0.0f, 20.0f, "Category rounding");
    addScalarRow(rows, "Rounding", "sidebar_rounding", p.sidebarRounding, 0.0f, 24.0f, "Sidebar rounding");
    addScalarRow(rows, "Rounding", "scrollbar_rounding", p.scrollbarRounding, 0.0f, 999.0f, "Scrollbar rounding");

    addScalarRow(rows, "Metrics", "scrollbar_size", p.scrollbarSize, 2.0f, 28.0f, "Scrollbar size");
    addScalarRow(rows, "Metrics", "window_padding_x", p.windowPaddingX, 0.0f, 40.0f, "Window padding X");
    addScalarRow(rows, "Metrics", "window_padding_y", p.windowPaddingY, 0.0f, 40.0f, "Window padding Y");
    addScalarRow(rows, "Metrics", "frame_padding_x", p.framePaddingX, 0.0f, 32.0f, "Frame padding X");
    addScalarRow(rows, "Metrics", "frame_padding_y", p.framePaddingY, 0.0f, 24.0f, "Frame padding Y");
    addScalarRow(rows, "Metrics", "item_spacing_x", p.itemSpacingX, 0.0f, 32.0f, "Item spacing X");
    addScalarRow(rows, "Metrics", "item_spacing_y", p.itemSpacingY, 0.0f, 32.0f, "Item spacing Y");
    addScalarRow(rows, "Metrics", "window_border_size", p.windowBorderSize, 0.0f, 4.0f, "Window border size");
    addScalarRow(rows, "Metrics", "frame_border_size", p.frameBorderSize, 0.0f, 4.0f, "Frame border size");
    addScalarRow(rows, "Metrics", "window_outline_size", p.windowOutlineSize, 0.0f, 4.0f, "Window outline size");
    addScalarRow(rows, "Metrics", "popup_outline_size", p.popupOutlineSize, 0.0f, 4.0f, "Popup outline size");

    overlaySavedRows(rows, theme.colors);
    return rows;
}

std::vector<ThemeColor> rowsToColors(const std::vector<ThemeRow>& rows)
{
    std::vector<ThemeColor> colors;
    colors.reserve(rows.size());
    for (const ThemeRow& row : rows) {
        ThemeColor color;
        color.key = row.key;
        if (row.type == ThemeRow::Type::Scalar) {
            color.color[0] = row.scalar;
            color.color[1] = 0.0f;
            color.color[2] = 0.0f;
            color.color[3] = 0.0f;
        } else {
            color.color[0] = row.color.x;
            color.color[1] = row.color.y;
            color.color[2] = row.color.z;
            color.color[3] = row.color.w;
        }
        colors.push_back(color);
    }
    return colors;
}

bool isAnimatedBackgroundSource(const std::filesystem::path& path)
{
    std::string ext = path.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return ext == ".gif" || ext == ".mp4" || ext == ".mov" || ext == ".mkv" || ext == ".webm" || ext == ".avi";
}

std::string openBackgroundFileDialog()
{
    wchar_t file[MAX_PATH]{};
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    if (ImGuiViewport* viewport = ImGui::GetWindowViewport()) {
        ofn.hwndOwner = static_cast<HWND>(viewport->PlatformHandleRaw);
    }
    const std::wstring title = trw("Select Background File");
    const std::wstring filter =
        fileDialogFilter({{"Images and videos", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp;*.mp4;*.mov;*.mkv;*.webm;*.avi"},
                          {"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.webp"},
                          {"Videos", L"*.mp4;*.mov;*.mkv;*.webm;*.avi"},
                          {"All Files", L"*.*"}});
    ofn.lpstrTitle = title.c_str();
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_ENABLESIZING | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (!GetOpenFileNameW(&ofn)) {
        return {};
    }
    return narrow(file);
}

void applyThemeEditorStyle(const ThemeDefinition& theme)
{
    const UiPalette p = uiPalette(theme);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, p.windowRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, p.popupRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, p.windowOutlineSize);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, p.popupOutlineSize);
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, p.windowBg);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.childBg);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(p.text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(p.textMuted));
    ImGui::PushStyleColor(ImGuiCol_Button, p.button);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.buttonHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, p.buttonActive);
    ImGui::PushStyleColor(ImGuiCol_Header, p.header);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, p.headerHovered);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, p.headerActive);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, p.frameBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, p.frameHovered);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, p.frameActive);
    ImGui::PushStyleColor(ImGuiCol_Border, p.windowOutline);
    ImGui::PushStyleColor(ImGuiCol_TableRowBg, p.windowBg);
    ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, p.childBg);
    ImGui::PushStyleColor(ImGuiCol_TableBorderLight, p.border);
    ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, p.border);
}

void popThemeEditorStyle()
{
    ImGui::PopStyleColor(18);
    ImGui::PopStyleVar(6);
}

void drawColorRow(ThemeRow& row, ImVec4& clipboardColor, const UiPalette& palette)
{
    ImGui::PushID(row.key.c_str());
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::Button("F##kind", ImVec2(26.0f, 28.0f));
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextUnformatted(tr(row.label.c_str()));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        beginStyledTooltip(ImGui::GetID((std::string("theme-tip-") + row.key).c_str()));
        ImGui::TextUnformatted(row.key.c_str());
        endStyledTooltip();
    }

    std::array<float*, 4> values = {&row.color.x, &row.color.y, &row.color.z, &row.color.w};
    std::array<const char*, 4> ids = {"##r", "##g", "##b", "##a"};
    for (int c = 0; c < 4; ++c) {
        ImGui::TableSetColumnIndex(c + 1);
        int value = static_cast<int>(std::clamp(*values[c], 0.0f, 1.0f) * 255.0f + 0.5f);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::DragInt(ids[c], &value, 1.0f, 0, 255, "%d")) {
            *values[c] = std::clamp(value / 255.0f, 0.0f, 1.0f);
        }
    }

    ImGui::TableSetColumnIndex(5);
    if (ImGui::ColorButton("##color", row.color, ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(32.0f, 28.0f))) {
        ImGui::OpenPopup("color-picker");
    }
    {
        LightPopupStyle colorPopupStyle(palette);
        const bool animatedPopup = ui_anim::pushPopupAppear("color-picker");
        if (ImGui::BeginPopup("color-picker")) {
            suppressCurrentViewportNativeBorder();
            ImGui::TextUnformatted(row.key.c_str());
            ImGui::Separator();
            ImGui::ColorPicker4("##picker", &row.color.x,
                                ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf | ImGuiColorEditFlags_DisplayRGB);
            ImGui::EndPopup();
        }
        if (animatedPopup) {
            ui_anim::popAppearAlpha();
        }
    }
    ImGui::TableSetColumnIndex(6);
    if (ui_anim::button(tr("Copy"), ImVec2(62.0f, 28.0f))) {
        clipboardColor = row.color;
    }
    ImGui::SameLine(0.0f, 8.0f);
    if (ui_anim::button(tr("Paste"), ImVec2(62.0f, 28.0f))) {
        row.color = clipboardColor;
    }
    ImGui::PopID();
}

void drawScalarRow(ThemeRow& row)
{
    ImGui::PushID(row.key.c_str());
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::Button("S##kind", ImVec2(26.0f, 28.0f));
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::TextUnformatted(tr(row.label.c_str()));
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        beginStyledTooltip(ImGui::GetID((std::string("theme-tip-") + row.key).c_str()));
        ImGui::TextUnformatted(row.key.c_str());
        endStyledTooltip();
    }

    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::DragFloat("##scalar", &row.scalar, 0.1f, row.minValue, row.maxValue, "%.1f px");
    for (int c = 2; c <= 6; ++c) {
        ImGui::TableSetColumnIndex(c);
        ImGui::TextDisabled(c == 5 ? "-" : "");
    }
    ImGui::PopID();
}

void drawThemeTable(std::vector<ThemeRow>& rows, ImVec4& clipboardColor, const UiPalette& palette)
{
    const ImU32 groupBg = ImGui::ColorConvertFloat4ToU32(palette.header);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 tableSize(std::max(0.0f, avail.x - 14.0f), std::max(0.0f, avail.y - 14.0f));
    if (!ImGui::BeginTable("theme-table", 7,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                               ImGuiTableFlags_SizingStretchProp,
                           tableSize)) {
        return;
    }

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(tr("Key"), ImGuiTableColumnFlags_WidthStretch, 0.46f);
    ImGui::TableSetupColumn("R", ImGuiTableColumnFlags_WidthFixed, 94.0f);
    ImGui::TableSetupColumn("G", ImGuiTableColumnFlags_WidthFixed, 94.0f);
    ImGui::TableSetupColumn("B", ImGuiTableColumnFlags_WidthFixed, 94.0f);
    ImGui::TableSetupColumn("A", ImGuiTableColumnFlags_WidthFixed, 94.0f);
    ImGui::TableSetupColumn(tr("Color"), ImGuiTableColumnFlags_WidthFixed, 78.0f);
    ImGui::TableSetupColumn(tr("Action"), ImGuiTableColumnFlags_WidthFixed, 160.0f);
    ImGui::TableHeadersRow();

    std::string currentGroup;
    for (int i = 0; i < static_cast<int>(rows.size());) {
        currentGroup = rows[i].group;
        ImGui::PushID(currentGroup.c_str());
        ImGui::TableNextRow(ImGuiTableRowFlags_None, 28.0f);
        ImGui::TableSetColumnIndex(0);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, groupBg);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, groupBg);
        const bool open = ImGui::TreeNodeEx("##group", ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_FramePadding, "%s",
                                            tr(currentGroup.c_str()));
        ImGui::PopID();

        const int groupStart = i;
        while (i < static_cast<int>(rows.size()) && rows[i].group == currentGroup) {
            ++i;
        }

        if (open) {
            for (int rowIndex = groupStart; rowIndex < i; ++rowIndex) {
                if (rows[rowIndex].type == ThemeRow::Type::Scalar) {
                    drawScalarRow(rows[rowIndex]);
                } else {
                    drawColorRow(rows[rowIndex], clipboardColor, palette);
                }
            }
            ImGui::TreePop();
        }
    }

    ImGui::EndTable();
}

void drawThemeMetadata(AppContext& context, ThemeDefinition& draft)
{
    static std::string backgroundCacheStatus;
    static bool backgroundCacheOk = false;
    static std::future<BackgroundCacheJobResult> backgroundCacheJob;

    const bool backgroundCacheRunning =
        backgroundCacheJob.valid() && backgroundCacheJob.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
    if (backgroundCacheJob.valid() && !backgroundCacheRunning) {
        const BackgroundCacheJobResult result = backgroundCacheJob.get();
        backgroundCacheOk = result.ok;
        backgroundCacheStatus = result.message;
    }

    if (!ImGui::BeginTable("theme-metadata-table", 4, ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 0.0f))) {
        return;
    }
    ImGui::TableSetupColumn("label-a", ImGuiTableColumnFlags_WidthFixed, 145.0f);
    ImGui::TableSetupColumn("value-a", ImGuiTableColumnFlags_WidthStretch, 0.48f);
    ImGui::TableSetupColumn("label-b", ImGuiTableColumnFlags_WidthFixed, 145.0f);
    ImGui::TableSetupColumn("value-b", ImGuiTableColumnFlags_WidthStretch, 0.52f);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(tr("Theme name"));
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##theme-name", &draft.name);
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(tr("Author"));
    ImGui::TableSetColumnIndex(3);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputText("##theme-author", &draft.author);

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(tr("Dark"));
    ImGui::TableSetColumnIndex(1);
    ImGui::Checkbox("##theme-dark", &draft.dark);
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(tr("Popup/Menu opacity"));
    ImGui::TableSetColumnIndex(3);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##popup-opacity", &draft.popupMenuOpacity, 0, 100, "%d%%");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(tr("Window transparency"));
    ImGui::TableSetColumnIndex(1);
    ImGui::Checkbox("##window-transparent", &draft.windowTransparent);
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(tr("Window opacity"));
    ImGui::TableSetColumnIndex(3);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##window-opacity", &draft.windowOpacity, 1, 100, "%d%%");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(tr("Apply transparency to all windows"));
    ImGui::TableSetColumnIndex(1);
    ImGui::Checkbox("##window-transparent-all", &draft.windowTransparencyForAll);
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(tr("Secondary window opacity"));
    ImGui::TableSetColumnIndex(3);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##secondary-window-opacity", &draft.secondaryWindowOpacity, 1, 100, "%d%%");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(tr("Background file"));
    ImGui::TableSetColumnIndex(1);
    ImGui::Checkbox("##background-enabled", &draft.background.enabled);
    ImGui::SameLine(0.0f, 8.0f);
    if (ui_anim::button(tr("Select file"), ImVec2(96.0f, 28.0f))) {
        const std::string selected = openBackgroundFileDialog();
        if (!selected.empty()) {
            draft.background.imagePath = selected;
            draft.background.imageEmbeddedBase64.clear();
            draft.background.imageEmbeddedName.clear();
            draft.background.imageEmbeddedMime.clear();
            draft.background.enabled = true;
            draft.background.animated = isAnimatedBackgroundSource(draft.background.imagePath);
            backgroundCacheStatus.clear();
        }
    }
    ImGui::SameLine(0.0f, 8.0f);
    if (ui_anim::button(tr("Clear"), ImVec2(68.0f, 28.0f))) {
        draft.background = {};
        backgroundCacheStatus.clear();
    }
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(tr("Source file"));
    ImGui::TableSetColumnIndex(3);
    ImGui::TextWrapped("%s", draft.background.imagePath.empty() ? tr("Not set") : draft.background.imagePath.filename().string().c_str());

    static constexpr std::array<const char*, 5> modes = {"Fill", "Fit", "Stretch", "Tile", "Center"};
    draft.background.imageMode = std::clamp(draft.background.imageMode, 0, static_cast<int>(modes.size()) - 1);
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(tr("Background image mode"));
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const UiPalette comboPalette = uiPalette(draft);
    const bool modeComboOpen =
        beginStyledCombo(comboPalette, "##background-mode", tr(modes[static_cast<size_t>(draft.background.imageMode)]));
    if (modeComboOpen) {
        for (int i = 0; i < static_cast<int>(modes.size()); ++i) {
            if (styledComboItem(comboPalette, tr(modes[static_cast<size_t>(i)]), draft.background.imageMode == i)) {
                draft.background.imageMode = i;
            }
        }
    }
    endStyledCombo(modeComboOpen);
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(tr("Background opacity"));
    ImGui::TableSetColumnIndex(3);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##background-opacity", &draft.background.opacity, 0, 100, "%d%%");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(tr("Animated background"));
    ImGui::TableSetColumnIndex(1);
    ImGui::Checkbox("##background-animated", &draft.background.animated);
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(tr("Animation FPS"));
    ImGui::TableSetColumnIndex(3);
    draft.background.animationFps = std::clamp(draft.background.animationFps, 1, 30);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##background-animation-fps", &draft.background.animationFps, 1, 30, "%d");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(tr("Animation max width"));
    ImGui::TableSetColumnIndex(1);
    draft.background.animationMaxWidth = std::clamp(draft.background.animationMaxWidth, 240, 3840);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##background-animation-width", &draft.background.animationMaxWidth, 240, 3840, "%d px");
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(tr("JPEG quality"));
    ImGui::TableSetColumnIndex(3);
    draft.background.animationQuality = std::clamp(draft.background.animationQuality, 2, 31);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::SliderInt("##background-animation-quality", &draft.background.animationQuality, 2, 31, "%d");

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::TextUnformatted(tr("Animation cache"));
    ImGui::TableSetColumnIndex(1);
    const bool canGenerate = draft.background.animated && !draft.background.imagePath.empty();
    ImGui::BeginDisabled(!canGenerate || backgroundCacheRunning);
    if (ui_anim::button(backgroundCacheRunning ? tr("Generating cache...") : tr("Generate cache"), ImVec2(148.0f, 28.0f))) {
        const std::filesystem::path source = draft.background.imagePath;
        const std::filesystem::path cacheRoot = context.config.directory() / "background-cache";
        const int fps = draft.background.animationFps;
        const int maxWidth = draft.background.animationMaxWidth;
        const int quality = draft.background.animationQuality;
        const std::string failedPrefix = tr("Animated background cache generation failed");
        const std::string successPrefix = tr("Animated background cache generated");
        const std::string framesText = tr("frames");
        backgroundCacheOk = true;
        backgroundCacheStatus = tr("Generating animated background cache...");
        backgroundCacheJob =
            std::async(std::launch::async, [source, cacheRoot, fps, maxWidth, quality, failedPrefix, successPrefix, framesText]() {
                std::string error;
                const bool ok = generateAnimatedBackgroundCache(source, cacheRoot, fps, maxWidth, quality, &error);
                if (!ok) {
                    return BackgroundCacheJobResult{false, failedPrefix + ": " + error};
                }
                const std::string key = animatedBackgroundCacheKey(source, fps, maxWidth, quality);
                const auto frames = animatedBackgroundFrames(animatedBackgroundCacheDirectory(cacheRoot, key));
                return BackgroundCacheJobResult{true, successPrefix + " (" + std::to_string(frames.size()) + " " + framesText + ")"};
            });
    }
    ImGui::EndDisabled();
    if (!backgroundCacheStatus.empty()) {
        ImGui::SameLine(0.0f, 8.0f);
        if (backgroundCacheOk) {
            ImGui::TextWrapped("%s", backgroundCacheStatus.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.35f, 0.25f, 1.0f), "%s", backgroundCacheStatus.c_str());
        }
    }
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(tr("Cache location"));
    ImGui::TableSetColumnIndex(3);
    if (draft.background.animated && !draft.background.imagePath.empty()) {
        const std::string key = animatedBackgroundCacheKey(draft.background.imagePath, draft.background.animationFps,
                                                           draft.background.animationMaxWidth, draft.background.animationQuality);
        ImGui::TextWrapped("%s", animatedBackgroundCacheDirectory(context.config.directory() / "background-cache", key).string().c_str());
    } else {
        ImGui::TextDisabled("%s", tr("Not set"));
    }
    ImGui::EndTable();
}

void trimEditorWorkingSet()
{
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

} // namespace

void drawThemeEditor(AppContext& context)
{
    static std::vector<ThemeRow> rows;
    static ThemeDefinition draft;
    static std::string loadedId;
    static std::filesystem::path loadedPath;
    static ImVec4 clipboardColor(1.0f, 1.0f, 1.0f, 1.0f);
    static bool wasVisible = false;
    static int trimFramesRemaining = 0;

    const auto clearEditorState = [&]() {
        rows.clear();
        rows.shrink_to_fit();
        draft = ThemeDefinition{};
        loadedId.clear();
        loadedPath.clear();
        clipboardColor = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    };

    if (!context.runtime().showThemeEditor) {
        if (wasVisible) {
            clearEditorState();
            trimFramesRemaining = 4;
            wasVisible = false;
        }
        if (trimFramesRemaining > 0) {
            trimEditorWorkingSet();
            --trimFramesRemaining;
        }
        return;
    }
    wasVisible = true;
    trimFramesRemaining = 0;

    const ThemeDefinition& active = context.themes.active();
    if (rows.empty() || loadedId != active.id || loadedPath != active.sourcePath) {
        draft = active;
        rows = makeRows(draft);
        loadedId = active.id;
        loadedPath = active.sourcePath;
    }

    draft.colors = rowsToColors(rows);
    draft.windowOpacity = std::clamp(draft.windowOpacity, 1, 100);
    draft.secondaryWindowOpacity = std::clamp(draft.secondaryWindowOpacity, 1, 100);
    draft.popupMenuOpacity = std::clamp(draft.popupMenuOpacity, 0, 100);
    draft.background.opacity = std::clamp(draft.background.opacity, 0, 100);
    context.themes.setPreview(draft);

    setupManagedWindow("LauncherThemeEditorViewport");
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowSize(ImVec2(1040.0f, 820.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    applyThemeEditorStyle(draft);
    bool open = true;
    if (ImGui::Begin(tr("Theme Editor###theme-editor"), &open,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        if (auto* hwnd = static_cast<HWND>(ImGui::GetWindowViewport()->PlatformHandleRaw)) {
            applyManagedViewportChrome(hwnd, draft, uiPalette(draft));
        }
        drawManagedTitleBar(uiPalette(draft), tr("Theme Editor"), open);
        if (!open) {
            context.runtime().showThemeEditor = false;
        }

        ImGui::SetCursorPos(ImVec2(14.0f, kUiTitleHeight + 10.0f));
        if (ui_anim::button(tr("Reload"), ImVec2(74.0f, 32.0f))) {
            context.themes.reload(context.themes.builtinDirectory(), context.themes.customDirectory(),
                                  context.persisted().settings.themeId);
            draft = context.themes.active();
            rows = makeRows(draft);
            loadedId = draft.id;
            loadedPath = draft.sourcePath;
        }
        ImGui::SameLine();
        if (ui_anim::button(tr("Load"), ImVec2(74.0f, 32.0f))) {
            ImGui::OpenPopup("load-theme-popup");
        }
        {
            LightPopupStyle loadPopupStyle(uiPalette(draft));
            const bool loadPopupAnimated = ui_anim::pushPopupAppear("load-theme-popup");
            if (ImGui::BeginPopup("load-theme-popup")) {
                suppressCurrentViewportNativeBorder();
                const UiPalette popupPalette = uiPalette(draft);
                for (const ThemeCatalogEntry& entry : context.themes.entries()) {
                    ImGui::PushID(entry.id.c_str());
                    std::string label = entry.name.empty() ? entry.id : entry.name;
                    label += "  (";
                    label += entry.builtin ? tr("Built-in") : tr("Custom");
                    label += ")";
                    const bool selected = entry.id == draft.id;
                    if (styledComboItem(popupPalette, label.c_str(), selected)) {
                        ThemeDefinition loaded;
                        if (context.themes.getTheme(entry.id, loaded)) {
                            draft = loaded;
                            rows = makeRows(draft);
                            loadedId = draft.id;
                            loadedPath = draft.sourcePath;
                            context.themes.setPreview(draft);
                        }
                        ImGui::CloseCurrentPopup();
                    }
                    ImGui::PopID();
                }
                ImGui::EndPopup();
            }
            if (loadPopupAnimated) {
                ui_anim::popAppearAlpha();
            }
        }
        ImGui::SameLine();
        if (ui_anim::button(draft.builtin ? tr("Save As") : tr("Save"), ImVec2(92.0f, 32.0f))) {
            draft.colors = rowsToColors(rows);
            std::string selectedId;
            if (context.themes.saveCustomTheme(draft, &selectedId)) {
                context.persisted().settings.themeId = selectedId;
                context.save();
                draft = context.themes.active();
                rows = makeRows(draft);
                loadedId = draft.id;
                loadedPath = draft.sourcePath;
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", draft.builtin ? tr("Built-in themes save as custom themes") : draft.sourcePath.string().c_str());

        ImGui::SetCursorPos(ImVec2(14.0f, kUiTitleHeight + 52.0f));
        ImGui::BeginChild("theme-metadata", ImVec2(-14.0f, 312.0f), ImGuiChildFlags_None);
        drawThemeMetadata(context, draft);
        ImGui::EndChild();

        ImGui::SetCursorPosX(14.0f);
        const UiPalette p = uiPalette(draft);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, p.frameRounding);
        drawThemeTable(rows, clipboardColor, p);
        ImGui::PopStyleVar();
    }
    ImGui::End();
    popThemeEditorStyle();

    if (!open) {
        context.runtime().showThemeEditor = false;
    }
}

} // namespace launcher
