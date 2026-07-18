#pragma once

#include "ui/common/UiTheme.hpp"

#include <string>

namespace launcher::settings_ui {

UiPalette currentControlPalette();
bool beginCombo(const UiPalette& palette, const char* id, const char* preview);
void endCombo(bool open);
bool comboItem(const UiPalette& palette, const char* label, bool selected);

void section(const char* title);
void helpMarker(const char* text);
bool rowCheckbox(const char* label, bool* value, float indent = 28.0f, const char* tooltip = nullptr);
bool rowSliderInt(const char* label, int* value, int min, int max, const char* format = "%d", float width = 220.0f,
                  const char* tooltip = nullptr);
bool rowCombo(const char* label, int* value, const char* items, float width = 220.0f, const char* tooltip = nullptr);
bool drawHotkeyInput(const char* id, std::string& hotkey);

} // namespace launcher::settings_ui
