#include "ui/MainDockItemEditor.hpp"

#include "app/AppContext.hpp"
#include "ui/Localization.hpp"
#include "ui/MainDockChrome.hpp"
#include "ui/MainDockWin32.hpp"
#include "ui/UiAnimation.hpp"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace launcher {
namespace {

ImVec4 colorVecFromItem(const LaunchItem& item)
{
    ImU32 color = 0;
    const std::string hex = item.fallbackColor.empty() ? "#8C8C8CFF" : item.fallbackColor;
    if (hex.size() == 9 && hex[0] == '#') {
        unsigned int rgba = 0;
        if (std::sscanf(hex.c_str() + 1, "%08x", &rgba) == 1) {
            color = IM_COL32((rgba >> 24) & 0xff, (rgba >> 16) & 0xff, (rgba >> 8) & 0xff, rgba & 0xff);
        }
    }
    if (color == 0) {
        color = IM_COL32(140, 140, 140, 255);
    }
    return ImGui::ColorConvertU32ToFloat4(color);
}

void commitTitleEdit(const ItemEditorApi& api, AppContext& context, ItemEditorState state)
{
    if (!api.editingItems || !api.selectSingle || !api.nowUnix || !state.editingDraft || !state.editingItem) {
        return;
    }
    if (std::vector<LaunchItem>* items = api.editingItems(context)) {
        state.editingDraft->lastEditedAt = api.nowUnix();
        if (state.editingDraft->createdAt <= 0) {
            state.editingDraft->createdAt = state.editingDraft->lastEditedAt;
        }
        if (*state.editingItem >= 0 && *state.editingItem < static_cast<int>(items->size())) {
            (*items)[*state.editingItem] = *state.editingDraft;
            api.selectSingle(context, (*items)[*state.editingItem]);
        } else {
            items->push_back(*state.editingDraft);
            api.selectSingle(context, items->back());
        }
        context.commitContentChange();
    }
}

void commitRegularEdit(const ItemEditorApi& api, AppContext& context, ItemEditorState state)
{
    if (!api.editingItems || !api.selectSingle || !api.nowUnix || !state.editingDraft || !state.editingItem || !state.editingTarget ||
        !state.editingStartDir || !state.editingRemark || !state.editingIcon) {
        return;
    }
    if (std::vector<LaunchItem>* items = api.editingItems(context)) {
        state.editingDraft->target = *state.editingTarget;
        state.editingDraft->startDirectory = *state.editingStartDir;
        state.editingDraft->subtitle = *state.editingRemark;
        state.editingDraft->remark = *state.editingRemark;
        state.editingDraft->icon = *state.editingIcon;
        state.editingDraft->lastEditedAt = api.nowUnix();
        if (state.editingDraft->createdAt <= 0) {
            state.editingDraft->createdAt = state.editingDraft->lastEditedAt;
        }
        if (*state.editingItem >= 0 && *state.editingItem < static_cast<int>(items->size())) {
            (*items)[*state.editingItem] = *state.editingDraft;
            api.selectSingle(context, (*items)[*state.editingItem]);
        } else {
            items->push_back(*state.editingDraft);
            api.selectSingle(context, items->back());
        }
        context.commitContentChange();
    }
}

bool comboItem(const UiPalette& theme, const char* label, bool selected)
{
    return styledComboItem(theme, label, selected);
}

bool comboFromItems(const UiPalette& theme, const char* id, int* value, const char* itemsKey)
{
    std::vector<const char*> itemList;
    for (const char* item = tr(itemsKey); item && item[0] != '\0'; item += std::strlen(item) + 1) {
        itemList.push_back(item);
    }
    if (itemList.empty()) {
        return false;
    }
    *value = std::clamp(*value, 0, static_cast<int>(itemList.size()) - 1);
    bool changed = false;
    const bool open = beginStyledCombo(theme, id, itemList[*value]);
    if (open) {
        ui_anim::pushAppearAlpha(ImGui::GetID(id), 0.10f, 0.20f);
        for (int i = 0; i < static_cast<int>(itemList.size()); ++i) {
            if (comboItem(theme, itemList[i], i == *value)) {
                *value = i;
                changed = true;
            }
        }
        ui_anim::popAppearAlpha();
    }
    endStyledCombo(open);
    return changed;
}

const char* interactiveKindLabel(InteractiveParamKind kind)
{
    switch (kind) {
    case InteractiveParamKind::Number: return tr("Number");
    case InteractiveParamKind::Choice: return tr("Choice");
    case InteractiveParamKind::Text:
    default: return tr("Text");
    }
}

bool interactiveKindCombo(const UiPalette& theme, const char* id, InteractiveParamKind* kind)
{
    bool changed = false;
    const bool open = beginStyledCombo(theme, id, interactiveKindLabel(*kind));
    if (open) {
        ui_anim::pushAppearAlpha(ImGui::GetID(id), 0.10f, 0.20f);
        const std::array<InteractiveParamKind, 3> kinds = {InteractiveParamKind::Text, InteractiveParamKind::Number,
                                                           InteractiveParamKind::Choice};
        for (InteractiveParamKind candidate : kinds) {
            if (comboItem(theme, interactiveKindLabel(candidate), candidate == *kind)) {
                *kind = candidate;
                changed = true;
            }
        }
        ui_anim::popAppearAlpha();
    }
    endStyledCombo(open);
    return changed;
}

std::string joinChoices(const std::vector<std::string>& choices)
{
    std::string text;
    for (const std::string& choice : choices) {
        if (!text.empty()) {
            text.push_back('\n');
        }
        text += choice;
    }
    return text;
}

std::vector<std::string> splitChoices(const std::string& text)
{
    std::vector<std::string> result;
    std::stringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
            line.pop_back();
        }
        if (!line.empty()) {
            result.push_back(line);
        }
    }
    return result;
}

std::string nextParamId(const std::vector<InteractiveParam>& params)
{
    int index = static_cast<int>(params.size()) + 1;
    while (true) {
        std::string id = "param" + std::to_string(index);
        const bool exists = std::any_of(params.begin(), params.end(), [&](const InteractiveParam& param) {
            return param.id == id;
        });
        if (!exists) {
            return id;
        }
        ++index;
    }
}

void drawInteractiveParamEditor(const UiPalette& theme, LaunchItem& item)
{
    ImGui::Dummy(ImVec2(1.0f, 6.0f));
    ImGui::SeparatorText(tr("Interactive Parameters"));
    ImGui::Checkbox(tr("Prompt before run"), &item.interactive);
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x);
    ImGui::TextDisabled("%s", tr("Use {{name}} or %name% in target, start directory, or arguments."));
    ImGui::PopTextWrapPos();

    if (!item.interactive) {
        ImGui::Dummy(ImVec2(1.0f, 8.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(1.0f, 6.0f));
        return;
    }

    ImGui::Dummy(ImVec2(1.0f, 4.0f));

    if (ImGui::Button(tr("Add Parameter"), ImVec2(130.0f, 28.0f))) {
        InteractiveParam param;
        param.id = nextParamId(item.interactiveParams);
        param.label = param.id;
        item.interactiveParams.push_back(std::move(param));
    }

    for (int i = 0; i < static_cast<int>(item.interactiveParams.size()); ++i) {
        InteractiveParam& param = item.interactiveParams[static_cast<size_t>(i)];
        ImGui::PushID(i);
        ImGui::Separator();
        ImGui::Text("%s %d", tr("Parameter"), i + 1);
        ImGui::SameLine();
        if (ImGui::Button(tr("Remove"), ImVec2(72.0f, 26.0f))) {
            item.interactiveParams.erase(item.interactiveParams.begin() + i);
            ImGui::PopID();
            --i;
            continue;
        }

        ImGui::TextUnformatted(tr("Name"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##param-id", &param.id);
        ImGui::TextUnformatted(tr("Label"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##param-label", &param.label);
        ImGui::TextUnformatted(tr("Type"));
        ImGui::SetNextItemWidth(-1.0f);
        interactiveKindCombo(theme, "##param-kind", &param.kind);
        ImGui::TextUnformatted(tr("Default Value"));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##param-default", &param.defaultValue);

        if (param.kind == InteractiveParamKind::Number) {
            ImGui::TextUnformatted(tr("Range"));
            ImGui::SetNextItemWidth(118.0f);
            ImGui::InputDouble("##param-min", &param.minValue, 0.0, 0.0, "%.3f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(118.0f);
            ImGui::InputDouble("##param-max", &param.maxValue, 0.0, 0.0, "%.3f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(96.0f);
            ImGui::InputDouble("##param-step", &param.step, 0.0, 0.0, "%.3f");
            if (param.maxValue < param.minValue) {
                std::swap(param.minValue, param.maxValue);
            }
            if (param.step <= 0.0) {
                param.step = 1.0;
            }
        } else if (param.kind == InteractiveParamKind::Choice) {
            ImGui::TextUnformatted(tr("Choices"));
            std::string choices = joinChoices(param.choices);
            if (ImGui::InputTextMultiline("##param-choices", &choices, ImVec2(-1.0f, 74.0f), ImGuiInputTextFlags_NoHorizontalScroll)) {
                param.choices = splitChoices(choices);
            }
        }
        ImGui::PopID();
    }
    ImGui::Dummy(ImVec2(1.0f, 8.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(1.0f, 6.0f));
}

} // namespace

void drawItemEditor(const UiPalette& theme, AppContext& context, ItemEditorState state, const ItemEditorApi& api)
{
    if (!state.showItemEditor || !*state.showItemEditor || !state.editingDraft || !state.editingCategory || !state.editingFolderId ||
        !state.editingItem || !state.editingTarget || !state.editingStartDir || !state.editingRemark || !state.editingIcon) {
        return;
    }

    setupManagedWindow("LauncherManagedItemEditor");

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 center = viewport->GetCenter();
    const ImVec2 editorSize(430.0f, std::min(680.0f, std::max(520.0f, viewport->WorkSize.y - 40.0f)));
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(editorSize, ImGuiCond_Appearing);
    if (state.openItemEditorPopup && *state.openItemEditorPopup) {
        ImGui::SetNextWindowFocus();
        *state.openItemEditorPopup = false;
    }
    ManagedWindowStyle windowStyle(theme);
    bool open = true;
    if (ImGui::Begin(tr("Edit###item-editor"), &open,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        applyManagedViewportChrome(ImGui::GetWindowViewport()->PlatformHandleRaw, context.themes.active(), theme);
        drawManagedTitleBar(theme, tr("Edit"), open);
        if (!open) {
            *state.showItemEditor = false;
            ImGui::End();
            return;
        }
        ImGui::SetCursorPos(ImVec2(12.0f, kUiTitleHeight + 10.0f));
        ImGui::BeginChild("edit-scroll", ImVec2(-12.0f, -58.0f), ImGuiChildFlags_None);
        constexpr ImGuiInputTextFlags wrapFlags = ImGuiInputTextFlags_NoHorizontalScroll;
        if (*state.editingItem < 0 && !context.persisted().categories.empty()) {
            *state.editingCategory = std::clamp(*state.editingCategory, 0, static_cast<int>(context.persisted().categories.size()) - 1);
            ImGui::TextUnformatted(tr("Category"));
            const std::string preview = context.persisted().categories[static_cast<size_t>(*state.editingCategory)].name;
            const bool categoryComboOpen = beginStyledCombo(theme, "##target-category", preview.c_str());
            if (categoryComboOpen) {
                ui_anim::pushAppearAlpha(ImGui::GetID("##target-category"), 0.10f, 0.20f);
                for (int i = 0; i < static_cast<int>(context.persisted().categories.size()); ++i) {
                    const bool selected = i == *state.editingCategory;
                    if (comboItem(theme, context.persisted().categories[static_cast<size_t>(i)].name.c_str(), selected)) {
                        *state.editingCategory = i;
                        state.editingFolderId->clear();
                    }
                }
                ui_anim::popAppearAlpha();
            }
            endStyledCombo(categoryComboOpen);
        }
        ImGui::TextUnformatted(tr("Name"));
        ImGui::SetNextItemWidth(-54.0f);
        ImGui::InputText("##name", &state.editingDraft->name);
        ImGui::SameLine();
        ImVec4 itemColor = colorVecFromItem(*state.editingDraft);
        if (ImGui::ColorButton("##item-color-preview", itemColor, ImGuiColorEditFlags_AlphaPreviewHalf, ImVec2(32.0f, 32.0f))) {
            ImGui::OpenPopup("item-color-picker");
        }
        {
            LightPopupStyle colorPopupStyle(theme);
            const bool colorPopupAnimated = ui_anim::pushPopupAppear("item-color-picker");
            if (ImGui::BeginPopup("item-color-picker")) {
                suppressCurrentViewportNativeBorder();
                if (ImGui::ColorPicker4("##item-color", &itemColor.x,
                                        ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
                    state.editingDraft->fallbackColor = colorHexFromVec(itemColor);
                }
                ImGui::EndPopup();
            }
            if (colorPopupAnimated) {
                ui_anim::popAppearAlpha();
            }
        }

        if (state.editingDraft->type == LaunchItemType::Title) {
            ImGui::TextUnformatted(tr("Title size"));
            ImGui::SetNextItemWidth(-1.0f);
            ui_anim::sliderInt("##title-size", &state.editingDraft->titleSize, 12, 48, "%d px");
            ImGui::TextUnformatted(tr("Title alignment"));
            ImGui::SetNextItemWidth(-1.0f);
            comboFromItems(theme, "##title-align", &state.editingDraft->titleAlign, "Title Alignment Items");
            ImGui::EndChild();

            ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 194.0f);
            if (ImGui::Button(tr("OK"), ImVec2(86.0f, 32.0f))) {
                commitTitleEdit(api, context, state);
                *state.showItemEditor = false;
            }
            ImGui::SameLine();
            if (ImGui::Button(tr("Cancel"), ImVec2(86.0f, 32.0f))) {
                *state.showItemEditor = false;
            }
            ImGui::End();
            return;
        }

        ImGui::TextUnformatted(tr("Target"));
        ImGui::InputTextMultiline("##target", state.editingTarget, ImVec2(-1.0f, 52.0f), wrapFlags);
        ImGui::TextUnformatted(tr("Start Directory"));
        ImGui::InputTextMultiline("##start-dir", state.editingStartDir, ImVec2(-1.0f, 52.0f), wrapFlags);
        ImGui::TextUnformatted(tr("Arguments"));
        ImGui::InputTextMultiline("##args", &state.editingDraft->arguments, ImVec2(-1.0f, 52.0f), wrapFlags);
        drawInteractiveParamEditor(theme, *state.editingDraft);
        ImGui::Checkbox(tr("Run as Administrator"), &state.editingDraft->runAsAdmin);
        ImGui::TextUnformatted(tr("Search Keywords"));
        ImGui::InputText("##keywords", &state.editingDraft->keywords);
        ImGui::TextUnformatted(tr("Process Priority"));
        comboFromItems(theme, "##priority", &state.editingDraft->priority, "Process Priority Items");
        ImGui::TextUnformatted(tr("Remark"));
        ImGui::InputTextMultiline("##remark", state.editingRemark, ImVec2(-1.0f, 66.0f), wrapFlags);
        ImGui::TextUnformatted(tr("Icon"));
        ImGui::InputTextMultiline("##icon", state.editingIcon, ImVec2(-62.0f, 110.0f), wrapFlags);
        ImGui::SameLine();
        ImGui::BeginGroup();
        if (ImGui::Button(tr("Select"), ImVec2(50.0f, 28.0f))) {
            std::string selected = openFileDialog(
                trw("Select Icon File").c_str(),
                fileDialogFilter({{"Icons and Images", L"*.ico;*.png;*.jpg;*.jpeg;*.bmp;*.exe;*.dll"}, {"All Files", L"*.*"}}).c_str());
            if (!selected.empty()) {
                *state.editingIcon = selected;
            }
        }
        if (ImGui::Button(tr("Browse"), ImVec2(50.0f, 28.0f))) {
            std::string selected = pickIconDialog(state.editingIcon->empty() ? *state.editingTarget : *state.editingIcon);
            if (!selected.empty()) {
                *state.editingIcon = selected;
            }
        }
        if (ImGui::Button(tr("Clear"), ImVec2(50.0f, 28.0f))) state.editingIcon->clear();
        ImGui::EndGroup();
        ImGui::EndChild();

        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 194.0f);
        if (ImGui::Button(tr("OK"), ImVec2(86.0f, 32.0f))) {
            commitRegularEdit(api, context, state);
            *state.showItemEditor = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"), ImVec2(86.0f, 32.0f))) {
            *state.showItemEditor = false;
        }
    }
    ImGui::End();
}

} // namespace launcher
