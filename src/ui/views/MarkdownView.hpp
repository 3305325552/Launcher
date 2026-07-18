#pragma once

#include "ui/common/UiTheme.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
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

struct MarkdownImage {
    std::uintptr_t textureId = 0;
    int width = 0;
    int height = 0;
};

using MarkdownImageResolver = std::function<std::optional<MarkdownImage>(std::string_view source)>;

std::vector<MarkdownOutlineItem> buildMarkdownOutline(const std::string& markdown);

void drawMarkdownPreview(const UiPalette& theme, const std::string& markdown, MarkdownOutlineUiState* scrollState = nullptr,
                         const MarkdownImageResolver* imageResolver = nullptr);
void drawSelectableMarkdownPreview(const UiPalette& theme, const std::string& markdown, MarkdownOutlineUiState* scrollState = nullptr,
                                   const MarkdownImageResolver* imageResolver = nullptr);
void drawMarkdownOutlinePanel(const UiPalette& theme, const std::string& markdown, MarkdownOutlineUiState& state);
void drawMarkdownDocument(const UiPalette& theme, const std::string& markdown, MarkdownOutlineUiState& state, bool selectable = true,
                          const MarkdownImageResolver* imageResolver = nullptr);
std::string markdownPlainText(const std::string& markdown);

} // namespace launcher
