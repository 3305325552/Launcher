#pragma once

#include "ui/UiTheme.hpp"

#include <string>
#include <vector>

namespace launcher {

struct MarkdownOutlineUiState {
    int pendingHeadingIndex = -1;
    int pendingLine = -1;
    int editorScrollFrames = 0;
    bool showOutline = true;
};

struct MarkdownOutlineItem {
    int level = 1;
    int line = 0;
    std::string title;
};

std::vector<MarkdownOutlineItem> buildMarkdownOutline(const std::string& markdown);

void drawMarkdownPreview(const UiPalette& theme, const std::string& markdown, MarkdownOutlineUiState* scrollState = nullptr);
void drawSelectableMarkdownPreview(const UiPalette& theme, const std::string& markdown, MarkdownOutlineUiState* scrollState = nullptr);
void drawMarkdownOutlinePanel(const UiPalette& theme, const std::string& markdown, MarkdownOutlineUiState& state);
void drawMarkdownDocument(const UiPalette& theme, const std::string& markdown, MarkdownOutlineUiState& state, bool selectable = true);
std::string markdownPlainText(const std::string& markdown);

} // namespace launcher
