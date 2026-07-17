#pragma once

#include "ui/UiTheme.hpp"

#include <imgui.h>

namespace launcher {

UiPalette withPopupOpacity(UiPalette theme, int opacityPercent);

class LightPopupStyle {
public:
    explicit LightPopupStyle(const UiPalette& theme, int opacityPercent = 100, float minWidth = 220.0f, float itemSpacingX = 10.0f);
    ~LightPopupStyle();

    LightPopupStyle(const LightPopupStyle&) = delete;
    LightPopupStyle& operator=(const LightPopupStyle&) = delete;
};

class ManagedWindowStyle {
public:
    explicit ManagedWindowStyle(const UiPalette& theme);
    ~ManagedWindowStyle();

    ManagedWindowStyle(const ManagedWindowStyle&) = delete;
    ManagedWindowStyle& operator=(const ManagedWindowStyle&) = delete;
};

void beginStyledTooltip(const UiPalette& theme, ImGuiID owner, float wrapWidth = 360.0f, float duration = 0.10f, float minAlpha = 0.14f,
                        int opacityPercent = 100);
void beginStyledTooltip(ImGuiID owner, float wrapWidth = 360.0f, float duration = 0.10f, float minAlpha = 0.14f, int opacityPercent = 100);
void endStyledTooltip();
bool beginStyledCombo(const UiPalette& theme, const char* id, const char* preview);
bool styledComboItem(const UiPalette& theme, const char* label, bool selected);
void endStyledCombo(bool open);

void setupManagedWindow(const char* classId);
void applyManagedViewportChrome(void* hwnd, const ThemeDefinition& theme, const UiPalette& palette);
void suppressCurrentViewportNativeBorder();
bool drawManagedTitleBar(const UiPalette& theme, const char* title, bool& open);
bool drawTitleButton(const UiPalette& theme, float titleHeight, const char* id, const ImVec2& pos, const char* icon, bool active = false);
void drawResizeHandles(const ImVec2& origin, const ImVec2& size, int minWindowWidth, int minWindowHeight);

} // namespace launcher
