#include "ui/MainDockChrome.hpp"

#include "ui/MaterialIcons.hpp"
#include "ui/UiAnimation.hpp"

#include <windows.h>
#include <dwmapi.h>
#include <imgui_internal.h>

#include <algorithm>
#include <string>
#include <unordered_map>

namespace launcher {
namespace {

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
#endif
#ifndef DWMWA_BORDER_COLOR
constexpr DWORD DWMWA_BORDER_COLOR = 34;
#endif
#ifndef DWMWA_COLOR_NONE
constexpr COLORREF DWMWA_COLOR_NONE_VALUE = 0xFFFFFFFE;
#else
constexpr COLORREF DWMWA_COLOR_NONE_VALUE = DWMWA_COLOR_NONE;
#endif

struct ManagedChromeState {
    bool borderApplied = false;
    int rounding = -1;
    bool transparent = false;
    int opacity = -1;
};

std::unordered_map<HWND, ManagedChromeState> gManagedChrome;
enum ResizeEdge {
    EdgeNone = 0,
    EdgeLeft = 1 << 0,
    EdgeRight = 1 << 1,
    EdgeTop = 1 << 2,
    EdgeBottom = 1 << 3
};

float colorLuminance(ImU32 color)
{
    const float r = static_cast<float>(color & 0xff) / 255.0f;
    const float g = static_cast<float>((color >> 8) & 0xff) / 255.0f;
    const float b = static_cast<float>((color >> 16) & 0xff) / 255.0f;
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

void suppressNativeBorder(HWND hwnd)
{
    if (!hwnd) {
        return;
    }
    const int roundPreference = 2;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &roundPreference, sizeof(roundPreference));
    const COLORREF borderColor = DWMWA_COLOR_NONE_VALUE;
    DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));
}

void applyViewportRounding(HWND hwnd, float rounding)
{
    if (!hwnd) {
        return;
    }
    const int roundPreference = rounding > 0.0f ? 2 : 1;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &roundPreference, sizeof(roundPreference));
    SetWindowRgn(hwnd, nullptr, TRUE);
}

void applySecondaryOpacity(HWND hwnd, const ThemeDefinition& theme)
{
    if (!hwnd) {
        return;
    }
    LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const int opacity = std::clamp(theme.secondaryWindowOpacity, 1, 100);
    const BYTE alpha = static_cast<BYTE>(std::clamp(opacity * 255 / 100, 1, 255));
    if (theme.windowTransparent && theme.windowTransparencyForAll && opacity < 100) {
        exStyle |= WS_EX_LAYERED;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
        SetLayeredWindowAttributes(hwnd, 0, alpha, LWA_ALPHA);
    } else {
        exStyle &= ~WS_EX_LAYERED;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
        RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME);
    }
}

ImVec4 popupTextColor(const ImVec4& background)
{
    const ImU32 packed = ImGui::ColorConvertFloat4ToU32(background);
    return colorLuminance(packed) > 0.52f ? ImVec4(0.18f, 0.18f, 0.18f, 1.0f) : ImVec4(0.90f, 0.90f, 0.90f, 1.0f);
}

ImVec4 popupMutedTextColor(const ImVec4& background)
{
    ImVec4 text = popupTextColor(background);
    text.w = 0.62f;
    return text;
}

} // namespace

UiPalette withPopupOpacity(UiPalette theme, int opacityPercent)
{
    const float opacity = std::clamp(opacityPercent, 0, 100) / 100.0f;
    theme.popupBg.w *= opacity;
    theme.popupOutline.w *= opacity;
    return theme;
}

LightPopupStyle::LightPopupStyle(const UiPalette& theme, int opacityPercent)
{
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * std::clamp(opacityPercent, 0, 100) / 100.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(220.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, theme.popupRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, theme.popupOutlineSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, theme.popupOutlineSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.popupRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, theme.popupRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.frameRounding);
    ImGui::PushStyleColor(ImGuiCol_Text, popupTextColor(theme.popupBg));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, popupMutedTextColor(theme.popupBg));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme.popupBg);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.popupOutline);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
}

LightPopupStyle::~LightPopupStyle()
{
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(10);
}

ManagedWindowStyle::ManagedWindowStyle(const UiPalette& theme)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.windowRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, theme.windowOutlineSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.windowBg);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.childBg);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(theme.textMuted));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, theme.frameBg);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme.frameHovered);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, theme.frameActive);
    ImGui::PushStyleColor(ImGuiCol_Button, theme.button);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.buttonHovered);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.buttonActive);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.windowOutline);
}

ManagedWindowStyle::~ManagedWindowStyle()
{
    ImGui::PopStyleColor(11);
    ImGui::PopStyleVar(3);
}

void beginStyledTooltip(const UiPalette& theme, ImGuiID owner, float wrapWidth, float duration, float minAlpha, int opacityPercent)
{
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * std::clamp(opacityPercent, 0, 100) / 100.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, theme.popupRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, theme.popupRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, theme.popupOutlineSize);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, theme.popupOutlineSize);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme.popupBg);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text));
    ImGui::PushStyleColor(ImGuiCol_Border, theme.popupOutline);
    ui_anim::pushAppearAlpha(owner, duration, minAlpha);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(wrapWidth);
}

void beginStyledTooltip(ImGuiID owner, float wrapWidth, float duration, float minAlpha, int opacityPercent)
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * std::clamp(opacityPercent, 0, 100) / 100.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, style.PopupRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, style.PopupRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, style.PopupBorderSize);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, style.PopupBorderSize);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, style.Colors[ImGuiCol_PopupBg]);
    ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_Text]);
    ImGui::PushStyleColor(ImGuiCol_Border, style.Colors[ImGuiCol_Border]);
    ui_anim::pushAppearAlpha(owner, duration, minAlpha);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(wrapWidth);
}

void endStyledTooltip()
{
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ui_anim::popAppearAlpha();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(6);
}

bool beginStyledCombo(const UiPalette& theme, const char* id, const char* preview)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 7.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, theme.popupRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, theme.popupOutlineSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, theme.popupOutlineSize);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.frameRounding);
    ImGui::PushStyleColor(ImGuiCol_PopupBg, theme.popupBg);
    ImGui::PushStyleColor(ImGuiCol_Border, theme.popupOutline);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(theme.text));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImGui::ColorConvertU32ToFloat4(theme.textMuted));
    ImGui::PushStyleColor(ImGuiCol_Header, theme.header);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, theme.headerHovered);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, theme.headerActive);
    return ImGui::BeginCombo(id, preview);
}

bool styledComboItem(const UiPalette& theme, const char* label, bool selected)
{
    ImGui::PushID(label);
    const float rowHeight = std::max(ImGui::GetFrameHeight(), ImGui::CalcTextSize(label).y + 10.0f);
    const float rowWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    const bool clicked = ImGui::Selectable("##combo-item", selected, ImGuiSelectableFlags_None, ImVec2(rowWidth, rowHeight));
    ImGui::PopStyleColor(3);

    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    if (selected || hovered || active) {
        ImVec4 color = active ? theme.headerActive : hovered ? theme.headerHovered : theme.header;
        color.w = std::max(color.w, selected ? 0.78f : 0.62f);
        ImGui::GetWindowDrawList()->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(color), theme.itemRounding);
    }

    const ImVec2 textSize = ImGui::CalcTextSize(label);
    ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 8.0f, min.y + (rowHeight - textSize.y) * 0.5f), theme.text, label);
    if (selected) {
        ImGui::SetItemDefaultFocus();
    }
    ImGui::PopID();
    return clicked;
}
void endStyledCombo(bool open)
{
    if (open) {
        ImGui::EndCombo();
    }
    ImGui::PopStyleColor(7);
    ImGui::PopStyleVar(6);
}

void suppressCurrentViewportNativeBorder()
{
    ImGuiViewport* viewport = ImGui::GetWindowViewport();
    if (!viewport) {
        return;
    }
    suppressNativeBorder(static_cast<HWND>(viewport->PlatformHandleRaw));
}

void applyManagedViewportChrome(void* rawHwnd, const ThemeDefinition& theme, const UiPalette& palette)
{
    auto* hwnd = static_cast<HWND>(rawHwnd);
    if (!hwnd) {
        return;
    }

    for (auto it = gManagedChrome.begin(); it != gManagedChrome.end();) {
        if (!IsWindow(it->first)) {
            it = gManagedChrome.erase(it);
        } else {
            ++it;
        }
    }

    ManagedChromeState& state = gManagedChrome[hwnd];
    if (!state.borderApplied) {
        suppressNativeBorder(hwnd);
        state.borderApplied = true;
    }

    const int rounding = std::clamp(static_cast<int>(palette.windowRounding + 0.5f), 0, 96);
    if (state.rounding != rounding) {
        applyViewportRounding(hwnd, palette.windowRounding);
        state.rounding = rounding;
    }

    const int opacity = std::clamp(theme.secondaryWindowOpacity, 1, 100);
    const bool transparent = theme.windowTransparent && theme.windowTransparencyForAll && opacity < 100;
    if (state.transparent != transparent || state.opacity != opacity) {
        applySecondaryOpacity(hwnd, theme);
        state.transparent = transparent;
        state.opacity = opacity;
    }
}

void setupManagedWindow(const char* classId)
{
    ImGuiWindowClass windowClass{};
    windowClass.ClassId = ImGui::GetID(classId);
    windowClass.ViewportFlagsOverrideSet =
        ImGuiViewportFlags_NoTaskBarIcon | ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&windowClass);
}

bool drawManagedTitleBar(const UiPalette& theme, const char* title, bool& open)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetWindowPos();
    const float width = ImGui::GetWindowWidth();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + kUiTitleHeight), theme.titleBar, theme.windowRounding,
                      ImDrawFlags_RoundCornersTop);
    dl->AddText(ImVec2(pos.x + 14.0f, pos.y + 13.0f), theme.text, title);

    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("managed-drag", ImVec2((std::max)(0.0f, width - 58.0f), kUiTitleHeight));
    if (ImGui::IsItemActivated()) {
        ImGui::StartMouseMovingWindow(ImGui::GetCurrentWindow());
    }

    ImGui::SetCursorScreenPos(ImVec2(pos.x + width - 58.0f, pos.y));
    const bool close = ImGui::InvisibleButton("managed-close", ImVec2(58.0f, kUiTitleHeight));
    if (ImGui::IsItemHovered()) {
        dl->AddRectFilled(ImVec2(pos.x + width - 58.0f, pos.y), ImVec2(pos.x + width, pos.y + kUiTitleHeight), theme.titleButtonHover,
                          theme.windowRounding, ImDrawFlags_RoundCornersTopRight);
    }
    ui_anim::rippleLastItem(theme, theme.windowRounding);
    dl->AddText(nullptr, 21.0f, ImVec2(pos.x + width - 36.0f, pos.y + 13.0f), theme.text, Icons::Close);
    if (close) {
        open = false;
    }
    return close;
}

bool drawTitleButton(const UiPalette& theme, float titleHeight, const char* id, const ImVec2& pos, const char* icon, bool active)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::SetCursorScreenPos(pos);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(58.0f, titleHeight));
    const bool hovered = ImGui::IsItemHovered();
    const float hoverT = ui_anim::hoverAmount(ImGui::GetID((std::string("title-btn-hover-") + id).c_str()), hovered);
    if (hoverT > 0.01f || active) {
        ImVec4 color = ImGui::ColorConvertU32ToFloat4(active ? theme.titleButtonActive : theme.titleButtonHover);
        color.w *= active ? 1.0f : hoverT;
        dl->AddRectFilled(pos, ImVec2(pos.x + 58.0f, pos.y + titleHeight), ImGui::ColorConvertFloat4ToU32(color));
    }
    ui_anim::rippleLastItem(theme, 0.0f);
    const ImVec2 text = ImGui::CalcTextSize(icon);
    dl->AddText(ImVec2(pos.x + (58.0f - text.x) * 0.5f, pos.y + (titleHeight - text.y) * 0.5f), theme.text, icon);
    return clicked;
}

void drawResizeHandles(const ImVec2& origin, const ImVec2& size, int minWindowWidth, int minWindowHeight)
{
    static int activeEdge = EdgeNone;
    static RECT startRect{};
    static ImVec2 startMouse{};

    auto startResize = [&](int edge) {
        activeEdge = edge;
        startMouse = ImGui::GetIO().MousePos;
        if (auto* hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw)) {
            GetWindowRect(hwnd, &startRect);
        }
    };

    auto handle = [&](const char* id, int edge, ImVec2 pos, ImVec2 handleSize, ImGuiMouseCursor cursor) {
        ImGui::SetCursorScreenPos(pos);
        ImGui::InvisibleButton(id, handleSize);
        if (ImGui::IsItemHovered() || activeEdge == edge) {
            ImGui::SetMouseCursor(cursor);
        }
        if (ImGui::IsItemActivated()) {
            startResize(edge);
        }
    };

    constexpr float grip = 8.0f;
    handle("resize-left", EdgeLeft, ImVec2(origin.x, origin.y + grip), ImVec2(grip, size.y - grip * 2.0f), ImGuiMouseCursor_ResizeEW);
    handle("resize-right", EdgeRight, ImVec2(origin.x + size.x - grip, origin.y + grip), ImVec2(grip, size.y - grip * 2.0f),
           ImGuiMouseCursor_ResizeEW);
    handle("resize-top", EdgeTop, ImVec2(origin.x + grip, origin.y), ImVec2(size.x - grip * 2.0f, grip), ImGuiMouseCursor_ResizeNS);
    handle("resize-bottom", EdgeBottom, ImVec2(origin.x + grip, origin.y + size.y - grip), ImVec2(size.x - grip * 2.0f, grip),
           ImGuiMouseCursor_ResizeNS);
    handle("resize-tl", EdgeTop | EdgeLeft, origin, ImVec2(grip, grip), ImGuiMouseCursor_ResizeNWSE);
    handle("resize-tr", EdgeTop | EdgeRight, ImVec2(origin.x + size.x - grip, origin.y), ImVec2(grip, grip), ImGuiMouseCursor_ResizeNESW);
    handle("resize-bl", EdgeBottom | EdgeLeft, ImVec2(origin.x, origin.y + size.y - grip), ImVec2(grip, grip), ImGuiMouseCursor_ResizeNESW);
    handle("resize-br", EdgeBottom | EdgeRight, ImVec2(origin.x + size.x - grip, origin.y + size.y - grip), ImVec2(grip, grip),
           ImGuiMouseCursor_ResizeNWSE);

    if (activeEdge != EdgeNone) {
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            activeEdge = EdgeNone;
            return;
        }
        ImVec2 delta = ImVec2(ImGui::GetIO().MousePos.x - startMouse.x, ImGui::GetIO().MousePos.y - startMouse.y);
        RECT rect = startRect;
        if (activeEdge & EdgeLeft) rect.left += static_cast<LONG>(delta.x);
        if (activeEdge & EdgeRight) rect.right += static_cast<LONG>(delta.x);
        if (activeEdge & EdgeTop) rect.top += static_cast<LONG>(delta.y);
        if (activeEdge & EdgeBottom) rect.bottom += static_cast<LONG>(delta.y);

        if (rect.right - rect.left < minWindowWidth) {
            if (activeEdge & EdgeLeft)
                rect.left = rect.right - minWindowWidth;
            else
                rect.right = rect.left + minWindowWidth;
        }
        if (rect.bottom - rect.top < minWindowHeight) {
            if (activeEdge & EdgeTop)
                rect.top = rect.bottom - minWindowHeight;
            else
                rect.bottom = rect.top + minWindowHeight;
        }

        if (auto* hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw)) {
            SetWindowPos(hwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOCOPYBITS);
        }
    }
}

} // namespace launcher
