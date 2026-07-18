#pragma once

#include "launcher/AppSettings.hpp"
#include "ui/common/UiTheme.hpp"

#include <imgui.h>

namespace launcher::ui_anim {

void beginFrame(const AppSettings& settings);
bool enabled();
float dt();

float tweenFloat(ImGuiID owner, const char* channel, float target, float duration, float initial = 0.0f);
ImVec2 tweenVec2(ImGuiID owner, const char* channel, ImVec2 target, float duration, ImVec2 initial = ImVec2(0.0f, 0.0f));
ImVec4 tweenColor(ImGuiID owner, const char* channel, ImVec4 target, float duration, ImVec4 initial);

ImVec2 layoutPos(ImGuiID owner, ImVec2 target, float duration = 0.16f);
void snapLayoutPos(ImGuiID owner, ImVec2 target);
void rebaseLayoutPos(ImGuiID owner, ImVec2 target);
float hoverAmount(ImGuiID owner, bool hovered, float duration = 0.12f);
float ghostAmount(ImGuiID owner);
float appearAmount(ImGuiID owner, float duration = 0.12f);
bool pushPopupAppear(const char* popupId, float duration = 0.12f);
void pushAppearAlpha(ImGuiID owner, float duration = 0.12f, float minAlpha = 0.0f);
void popAppearAlpha();

bool checkbox(const char* label, bool* value);
bool button(const char* label, ImVec2 size = ImVec2(0.0f, 0.0f));
bool sliderInt(const char* label, int* value, int min, int max, const char* format = "%d");
void rippleLastItem(const UiPalette& theme, float rounding);
void rippleLastItemInRect(const UiPalette& theme, const ImVec2& min, const ImVec2& max, float rounding);

} // namespace launcher::ui_anim
