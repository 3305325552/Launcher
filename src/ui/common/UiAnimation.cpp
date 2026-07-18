#include "ui/common/UiAnimation.hpp"

#include <im_anim.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace launcher::ui_anim {
namespace {

struct Ripple {
    ImGuiID owner = 0;
    ImVec2 center{};
    double startedAt = 0.0;
};

bool gEnabled = true;
float gDt = 1.0f / 60.0f;
float gSpeed = 1.0f;
int gFrame = 0;
constexpr int kAppearRestartGapFrames = 6;
std::vector<Ripple> gRipples;
std::unordered_map<ImGuiID, float> gFloatTargets;
std::unordered_map<ImGuiID, ImVec2> gVec2Targets;
std::unordered_map<ImGuiID, ImVec4> gColorTargets;
std::unordered_map<ImGuiID, int> gAppearSeenFrame;
std::unordered_map<ImGuiID, double> gAppearStartedAt;

iam_ease_desc easeOut()
{
    return iam_ease_preset(iam_ease_out_cubic);
}

ImGuiID channelId(const char* channel)
{
    return ImHashStr(channel);
}

float scaledDuration(float seconds)
{
    if (!gEnabled) {
        return 0.0f;
    }
    return std::max(0.001f, seconds / std::max(0.1f, gSpeed));
}

ImVec4 withAlpha(ImVec4 color, float alpha)
{
    color.w *= alpha;
    return color;
}

ImGuiID tweenKey(ImGuiID owner, const char* channel)
{
    const ImGuiID channelHash = channelId(channel);
    return ImHashData(&channelHash, sizeof(channelHash), owner);
}

bool sameVec2(ImVec2 a, ImVec2 b)
{
    return std::abs(a.x - b.x) + std::abs(a.y - b.y) <= 0.01f;
}

bool sameVec4(ImVec4 a, ImVec4 b)
{
    return std::abs(a.x - b.x) + std::abs(a.y - b.y) + std::abs(a.z - b.z) + std::abs(a.w - b.w) <= 0.001f;
}

} // namespace

void beginFrame(const AppSettings& settings)
{
    gEnabled = settings.enableAnimations;
    gSpeed = std::clamp(settings.animationSpeedPercent / 100.0f, 0.25f, 2.5f);
    gDt = ImGui::GetIO().DeltaTime * gSpeed;
    if (gEnabled) {
        iam_set_global_time_scale(gSpeed);
        iam_update_begin_frame();
        ++gFrame;
        if ((gFrame % 900) == 0) {
            iam_gc(900);
            for (auto it = gAppearSeenFrame.begin(); it != gAppearSeenFrame.end();) {
                if (gFrame - it->second > 900) {
                    gAppearStartedAt.erase(it->first);
                    it = gAppearSeenFrame.erase(it);
                } else {
                    ++it;
                }
            }
        }
    } else {
        gFloatTargets.clear();
        gVec2Targets.clear();
        gColorTargets.clear();
        gAppearSeenFrame.clear();
        gAppearStartedAt.clear();
    }
}

bool enabled()
{
    return gEnabled;
}

float dt()
{
    return gDt;
}

float tweenFloat(ImGuiID owner, const char* channel, float target, float duration, float initial)
{
    if (!gEnabled) {
        return target;
    }
    const ImGuiID key = tweenKey(owner, channel);
    auto it = gFloatTargets.find(key);
    if (it == gFloatTargets.end()) {
        gFloatTargets.emplace(key, target);
        return target;
    }
    const float start = it->second;
    if (std::abs(start - target) > 0.0001f) {
        iam_rebase_float(owner, channelId(channel), target, gDt);
        it->second = target;
    }
    return iam_tween_float(owner, channelId(channel), target, scaledDuration(duration), easeOut(), iam_policy_crossfade, gDt, start);
}

ImVec2 tweenVec2(ImGuiID owner, const char* channel, ImVec2 target, float duration, ImVec2 initial)
{
    if (!gEnabled) {
        return target;
    }
    const ImGuiID key = tweenKey(owner, channel);
    auto it = gVec2Targets.find(key);
    if (it == gVec2Targets.end()) {
        gVec2Targets.emplace(key, target);
        if (sameVec2(initial, target)) {
            return target;
        }
        return iam_tween_vec2(owner, channelId(channel), target, scaledDuration(duration), easeOut(), iam_policy_crossfade, gDt, initial);
    }
    const ImVec2 start = it->second;
    if (!sameVec2(start, target)) {
        iam_rebase_vec2(owner, channelId(channel), target, gDt);
        it->second = target;
    }
    return iam_tween_vec2(owner, channelId(channel), target, scaledDuration(duration), easeOut(), iam_policy_crossfade, gDt, start);
}

ImVec4 tweenColor(ImGuiID owner, const char* channel, ImVec4 target, float duration, ImVec4 initial)
{
    if (!gEnabled) {
        return target;
    }
    const ImGuiID key = tweenKey(owner, channel);
    auto it = gColorTargets.find(key);
    if (it == gColorTargets.end()) {
        gColorTargets.emplace(key, target);
        return target;
    }
    const ImVec4 start = it->second;
    if (!sameVec4(start, target)) {
        iam_rebase_color(owner, channelId(channel), target, gDt);
        it->second = target;
    }
    return iam_tween_color(owner, channelId(channel), target, scaledDuration(duration), easeOut(), iam_policy_crossfade, iam_col_oklab, gDt,
                           start);
}

ImVec2 layoutPos(ImGuiID owner, ImVec2 target, float duration)
{
    ImVec2 windowPos(0.0f, 0.0f);
    if (ImGuiWindow* window = ImGui::GetCurrentWindowRead()) {
        windowPos = window->Pos;
    }
    const ImVec2 localTarget(target.x - windowPos.x, target.y - windowPos.y);
    const ImVec2 localVisual = tweenVec2(owner, "layout-pos-local", localTarget, duration, localTarget);
    return ImVec2(localVisual.x + windowPos.x, localVisual.y + windowPos.y);
}

void snapLayoutPos(ImGuiID owner, ImVec2 target)
{
    if (!gEnabled) {
        return;
    }
    ImVec2 windowPos(0.0f, 0.0f);
    if (ImGuiWindow* window = ImGui::GetCurrentWindowRead()) {
        windowPos = window->Pos;
    }
    const ImVec2 localTarget(target.x - windowPos.x, target.y - windowPos.y);
    gVec2Targets[tweenKey(owner, "layout-pos-local")] = localTarget;
    iam_tween_vec2(owner, channelId("layout-pos-local"), localTarget, 0.001f, easeOut(), iam_policy_cut, gDt, localTarget);
}

void rebaseLayoutPos(ImGuiID owner, ImVec2 target)
{
    if (!gEnabled) {
        return;
    }
    ImVec2 windowPos(0.0f, 0.0f);
    if (ImGuiWindow* window = ImGui::GetCurrentWindowRead()) {
        windowPos = window->Pos;
    }
    const ImVec2 localTarget(target.x - windowPos.x, target.y - windowPos.y);
    gVec2Targets[tweenKey(owner, "layout-pos-local")] = localTarget;
    iam_rebase_vec2(owner, channelId("layout-pos-local"), localTarget, gDt);
}

float hoverAmount(ImGuiID owner, bool hovered, float duration)
{
    return tweenFloat(owner, "hover", hovered ? 1.0f : 0.0f, duration, hovered ? 1.0f : 0.0f);
}

float ghostAmount(ImGuiID owner)
{
    if (!gEnabled) {
        return 1.0f;
    }
    const float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(ImGui::GetTime()) * 7.0f);
    return tweenFloat(owner, "ghost-alpha", 0.62f + pulse * 0.18f, 0.10f, 0.62f);
}

float appearAmount(ImGuiID owner, float duration)
{
    if (!gEnabled) {
        return 1.0f;
    }
    int& seen = gAppearSeenFrame[owner];
    double& startedAt = gAppearStartedAt[owner];
    if (seen == 0 || gFrame - seen > kAppearRestartGapFrames) {
        startedAt = ImGui::GetTime();
    }
    seen = gFrame;
    const float t = std::clamp(static_cast<float>((ImGui::GetTime() - startedAt) / scaledDuration(duration)), 0.0f, 1.0f);
    return 1.0f - std::pow(1.0f - t, 3.0f);
}

bool pushPopupAppear(const char* popupId, float duration)
{
    if (!gEnabled || !ImGui::IsPopupOpen(popupId)) {
        return false;
    }
    pushAppearAlpha(ImGui::GetID(popupId), duration, 0.18f);
    return true;
}

void pushAppearAlpha(ImGuiID owner, float duration, float minAlpha)
{
    const float alpha = std::clamp(minAlpha + (1.0f - minAlpha) * appearAmount(owner, duration), 0.0f, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
}

void popAppearAlpha()
{
    ImGui::PopStyleVar();
}

bool checkbox(const char* label, bool* value)
{
    ImGui::PushID(label);
    const ImGuiID id = ImGui::GetID("animated-checkbox");
    const ImVec2 start = ImGui::GetCursorScreenPos();
    constexpr float box = 26.0f;
    const ImVec2 labelSize = ImGui::CalcTextSize(label);
    const ImVec2 size(box + 8.0f + labelSize.x, std::max(box, labelSize.y));
    ImGui::InvisibleButton("hit", size);
    const bool clicked = ImGui::IsItemClicked();
    if (clicked) {
        *value = !*value;
    }
    const bool hovered = ImGui::IsItemHovered();
    const float t = tweenFloat(id, "checked", *value ? 1.0f : 0.0f, 0.16f, *value ? 1.0f : 0.0f);
    const float h = hoverAmount(id, hovered);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 off = ImLerp(ImGui::GetStyleColorVec4(ImGuiCol_FrameBg), ImGui::GetStyleColorVec4(ImGuiCol_FrameBgHovered), h);
    const ImVec4 on(0.25f, 0.56f, 0.90f, 1.0f);
    const ImVec4 bg = ImLerp(off, on, t);
    const ImU32 bgColor = ImGui::ColorConvertFloat4ToU32(bg);
    const ImRect rect(start, ImVec2(start.x + box, start.y + box));
    dl->AddRectFilled(rect.Min, rect.Max, bgColor, 4.0f);
    if (t > 0.01f) {
        const ImU32 check = ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, t));
        const ImVec2 a(rect.Min.x + 6.5f, rect.Min.y + 13.5f);
        const ImVec2 b(rect.Min.x + 11.0f, rect.Min.y + 18.0f);
        const ImVec2 c(rect.Min.x + 20.0f, rect.Min.y + 8.0f);
        dl->AddLine(a, b, check, 3.0f);
        dl->AddLine(b, c, check, 3.0f);
    }
    dl->AddText(ImVec2(start.x + box + 8.0f, start.y + (box - labelSize.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_Text), label);
    ImGui::PopID();
    return clicked;
}

bool button(const char* label, ImVec2 size)
{
    const bool clicked = ImGui::Button(label, size);
    rippleLastItem(UiPalette{}, ImGui::GetStyle().FrameRounding);
    return clicked;
}

bool sliderInt(const char* label, int* value, int min, int max, const char* format)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->SkipItems) {
        return false;
    }

    ImGuiContext& g = *GImGui;
    const ImGuiStyle& style = g.Style;
    const ImGuiID id = window->GetID(label);
    const float width = ImGui::CalcItemWidth();
    const ImVec2 labelSize = ImGui::CalcTextSize(label, nullptr, true);
    const ImVec2 pos = window->DC.CursorPos;
    const float rowHeight = ImGui::GetFrameHeight();
    const float height = std::max(18.0f, ImGui::GetTextLineHeight() + 3.0f);
    const float frameY = pos.y + (rowHeight - height) * 0.5f;
    const ImRect frame(ImVec2(pos.x, frameY), ImVec2(pos.x + width, frameY + height));
    const ImRect total(pos,
                       ImVec2(pos.x + width + (labelSize.x > 0.0f ? style.ItemInnerSpacing.x + labelSize.x : 0.0f), pos.y + rowHeight));

    ImGui::ItemSize(total, style.FramePadding.y);
    if (!ImGui::ItemAdd(total, id, &frame)) {
        return false;
    }

    bool hovered = false;
    bool held = false;
    const bool pressed = ImGui::ButtonBehavior(frame, id, &hovered, &held);
    bool changed = false;
    if ((pressed || held) && max > min && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float ratio = std::clamp((ImGui::GetIO().MousePos.x - frame.Min.x) / std::max(1.0f, frame.GetWidth()), 0.0f, 1.0f);
        const int next = min + static_cast<int>(std::lround(ratio * static_cast<float>(max - min)));
        if (next != *value) {
            *value = std::clamp(next, min, max);
            ImGui::MarkItemEdited(id);
            changed = true;
        }
    }

    const float target = max > min ? std::clamp((*value - min) / static_cast<float>(max - min), 0.0f, 1.0f) : 0.0f;
    const float visual = tweenFloat(id, "slider-visual", target, 0.11f, target);
    const float rounding = style.FrameRounding;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 bg = ImGui::GetColorU32(held ? ImGuiCol_FrameBgActive : hovered ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
    const ImU32 fill = ImGui::GetColorU32(ImGuiCol_SliderGrabActive);
    const ImU32 grab = ImGui::GetColorU32(ImGuiCol_SliderGrab);
    dl->AddRectFilled(frame.Min, frame.Max, bg, rounding);
    dl->AddRectFilled(frame.Min, ImVec2(frame.Min.x + frame.GetWidth() * visual, frame.Max.y), fill, rounding);

    const float grabW = std::clamp(frame.GetHeight() * 0.36f, 7.0f, 11.0f);
    const float grabX = std::clamp(frame.Min.x + frame.GetWidth() * visual, frame.Min.x + grabW * 0.5f, frame.Max.x - grabW * 0.5f);
    const ImRect grabRect(ImVec2(grabX - grabW * 0.5f, frame.Min.y + 2.0f), ImVec2(grabX + grabW * 0.5f, frame.Max.y - 2.0f));
    dl->AddRectFilled(grabRect.Min, grabRect.Max, grab, std::max(2.0f, rounding - 1.0f));

    char valueText[64]{};
    ImFormatString(valueText, IM_ARRAYSIZE(valueText), format ? format : "%d", *value);
    ImGui::RenderTextClipped(frame.Min, frame.Max, valueText, nullptr, nullptr, ImVec2(0.5f, 0.5f));
    if (labelSize.x > 0.0f) {
        ImGui::RenderText(ImVec2(frame.Max.x + style.ItemInnerSpacing.x, frame.Min.y + style.FramePadding.y), label);
    }
    return changed;
}

void rippleLastItem(const UiPalette& theme, float rounding)
{
    rippleLastItemInRect(theme, ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), rounding);
}

void rippleLastItemInRect(const UiPalette& theme, const ImVec2& min, const ImVec2& max, float rounding)
{
    if (!gEnabled) {
        return;
    }
    const ImGuiID owner = ImGui::GetItemID();
    const ImRect rect(min, max);
    if (ImGui::IsItemActivated()) {
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        gRipples.push_back(Ripple{owner, ImVec2(std::clamp(mouse.x, rect.Min.x, rect.Max.x), std::clamp(mouse.y, rect.Min.y, rect.Max.y)),
                                  ImGui::GetTime()});
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const double now = ImGui::GetTime();
    const ImVec2 center = rect.GetCenter();
    for (auto it = gRipples.begin(); it != gRipples.end();) {
        const float age = static_cast<float>(now - it->startedAt);
        if (age > 0.42f) {
            it = gRipples.erase(it);
            continue;
        }
        if (it->owner != owner) {
            ++it;
            continue;
        }
        const float progress = std::clamp(age / 0.42f, 0.0f, 1.0f);
        const float eased = iam_eval_preset(iam_ease_out_cubic, progress);
        const float radius = std::max(rect.GetWidth(), rect.GetHeight()) * (0.18f + eased * 0.90f);
        const float alpha = (1.0f - progress) * 0.20f;
        const ImVec4 base = theme.rippleColor != 0
                                ? ImGui::ColorConvertU32ToFloat4(theme.rippleColor)
                                : (theme.buttonHovered.w > 0.0f ? theme.buttonHovered : ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
        if (rounding > 0.0f) {
            dl->AddRectFilled(rect.Min, rect.Max, ImGui::ColorConvertFloat4ToU32(withAlpha(base, alpha * 0.7f)), rounding);
        } else {
            dl->PushClipRect(rect.Min, rect.Max, true);
            dl->AddCircleFilled(it->center, radius, ImGui::ColorConvertFloat4ToU32(withAlpha(base, alpha)), 36);
            dl->PopClipRect();
        }
        dl->AddRect(rect.Min, rect.Max, ImGui::ColorConvertFloat4ToU32(withAlpha(base, alpha * 0.9f)), rounding);
        ++it;
    }
}

} // namespace launcher::ui_anim
