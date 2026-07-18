#include "ui/views/UserGuideView.hpp"

#include "app/AppContext.hpp"
#include "platform/SystemIntegration.hpp"
#include "ui/common/Localization.hpp"
#include "ui/dock/MainDockChrome.hpp"
#include "ui/views/MarkdownView.hpp"
#include "ui/common/MaterialIcons.hpp"
#include "ui/common/UiAnimation.hpp"

#include <windows.h>
#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace launcher {
namespace {

MarkdownOutlineUiState gUserGuideOutline;
MarkdownOutlineUiState gUserGuideInlineOutline;

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

const std::string& userGuideMarkdown()
{
    static std::string cached;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        cached = readTextFile(getAssetDir() / "docs" / "USER_GUIDE.md");
        if (cached.empty()) {
            cached = "# Launcher User Guide\n\nThe user guide was not found in `assets/docs/USER_GUIDE.md`.";
        }
    }
    return cached;
}

bool outlineToggleButton(const UiPalette& theme, bool active)
{
    ImGui::PushID("user-guide-outline-toggle");
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const std::string text = std::string(Icons::Layout) + "  " + tr("Outline");
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    const ImVec2 size(textSize.x + 18.0f, 28.0f);
    ImGui::InvisibleButton("button", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg = ImGui::ColorConvertFloat4ToU32(active ? theme.headerActive : (hovered ? theme.buttonHovered : theme.button));
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, theme.frameRounding);
    dl->AddText(ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f), theme.text, text.c_str());
    ImGui::PopID();
    return clicked;
}

} // namespace

void drawUserGuideInline(const UiPalette& theme)
{
    if (outlineToggleButton(theme, gUserGuideInlineOutline.showOutline)) {
        gUserGuideInlineOutline.showOutline = !gUserGuideInlineOutline.showOutline;
    }
    ImGui::Dummy(ImVec2(1.0f, 6.0f));
    drawMarkdownDocument(theme, userGuideMarkdown(), gUserGuideInlineOutline, true);
}

void drawUserGuideWindow(AppContext& context, const UiPalette& theme)
{
    if (!context.runtime().showUserGuide) {
        return;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGuiWindowClass guideClass{};
    guideClass.ClassId = ImGui::GetID("LauncherUserGuideViewport");
    guideClass.ViewportFlagsOverrideSet =
        ImGuiViewportFlags_NoTaskBarIcon | ImGuiViewportFlags_NoDecoration | ImGuiViewportFlags_NoAutoMerge;
    ImGui::SetNextWindowClass(&guideClass);
    const ImVec2 guideSize(std::min(980.0f, std::max(720.0f, viewport->WorkSize.x - 48.0f)),
                           std::min(760.0f, std::max(540.0f, viewport->WorkSize.y - 48.0f)));
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(guideSize, ImGuiCond_Appearing);
    ImGui::SetNextWindowFocus();

    ManagedWindowStyle style(theme);
    bool open = true;
    if (ImGui::Begin("User Guide", &open,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                         ImGuiWindowFlags_NoSavedSettings)) {
        if (auto* hwnd = static_cast<HWND>(ImGui::GetWindowViewport()->PlatformHandleRaw)) {
            applyManagedViewportChrome(hwnd, context.themes.active(), theme);
        }
        bool titleOpen = true;
        drawManagedTitleBar(theme, "User Guide", titleOpen);
        if (!titleOpen) {
            context.runtime().showUserGuide = false;
            ImGui::End();
            return;
        }

        ImGui::SetCursorPos(ImVec2(16.0f, kUiTitleHeight + 10.0f));
        if (outlineToggleButton(theme, gUserGuideOutline.showOutline)) {
            gUserGuideOutline.showOutline = !gUserGuideOutline.showOutline;
        }
        ImGui::SetCursorPos(ImVec2(16.0f, kUiTitleHeight + 46.0f));
        ImGui::BeginChild("user-guide-markdown", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None);
        drawMarkdownDocument(theme, userGuideMarkdown(), gUserGuideOutline, true);
        ImGui::EndChild();
    }
    ImGui::End();
    if (!open) {
        context.runtime().showUserGuide = false;
    }
}

} // namespace launcher
