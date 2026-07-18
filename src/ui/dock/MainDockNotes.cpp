#include "ui/dock/MainDockNotes.hpp"

#include "app/AppContext.hpp"
#include "ui/common/Localization.hpp"
#include "ui/dock/MainDockChrome.hpp"
#include "ui/dock/MainDockDragPayload.hpp"
#include "ui/dock/MainDockMenu.hpp"
#include "ui/views/MarkdownView.hpp"
#include "ui/common/MaterialIcons.hpp"
#include "ui/common/UiAnimation.hpp"

#include <windows.h>
#include <shellapi.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace launcher {
namespace {

struct NotesPanelState {
    std::string loadedNoteId;
    std::string draftTitle;
    std::string draftTags;
    std::string draftBody;
    bool draftPinned = false;
    bool draftFixed = false;
    std::string filter;
    std::string selectedFolder;
    std::unordered_map<std::string, bool> folderExpanded;
    bool includeArchived = false;
    bool dirty = false;
    double editedAt = 0.0;
    int mode = 2;
    float splitScrollRatio = 0.0f;
    MarkdownOutlineUiState outline;
    std::string statusMessage;
    double statusUntil = 0.0;

    std::unordered_set<std::string> selectedNoteIds;
    std::string dragPrimaryNoteId;
    std::vector<std::string> dragNoteIdsSnapshot;
    std::unordered_map<std::string, ImVec2> lastTreeRowPositions;
    std::unordered_map<std::string, ImVec2> pendingDropPositions;
    std::optional<ImVec2> dragPreviewAnchor;
    std::string rangeAnchorNoteId;
    std::string pendingRangeToNoteId;
    std::vector<std::string> lastVisibleNoteIds;
    std::string pressNoteId;
    bool pressWasMulti = false;
    bool pressHandledAsDrag = false;

    bool renameOpen = false;
    bool renameIsFolder = false;
    std::string renameTarget;
    std::string renameDraft;

    bool newFolderOpen = false;
    std::string newFolderParent;
    std::string newFolderDraft;

    // Internal note clipboard (copy/cut/paste notes in the tree).
    std::vector<Note> noteClipboard;
    bool noteClipboardCut = false;
    bool closeConfirmOpen = false;
    bool closeConfirmIsQuick = false;
    bool switchConfirmOpen = false;
    std::string pendingSwitchNoteId;
    int textSelStart = 0;
    int textSelEnd = 0;
    ImGuiID textFieldId = 0;
    bool textMenuOpen = false;
};

struct NoteTreeNode {
    bool isFolder = false;
    bool pinned = false;
    bool fixed = false;
    std::string name;
    std::string folderPath;
    std::string noteId;
    std::vector<NoteTreeNode> children;
};

NotesPanelState gNotesPanel;
bool gNotesPanelWasVisible = false;
int gNotesTrimFramesRemaining = 0;
constexpr float kOuterPadding = 14.0f;
constexpr float kPaneGap = 10.0f;
constexpr float kToolbarHeight = 34.0f;
constexpr float kTreeRowHeight = 28.0f;
constexpr float kTreeWidthMin = 220.0f;
constexpr float kTreeWidthMax = 300.0f;
constexpr float kOutlineWidthMin = 160.0f;
constexpr float kOutlineWidthMax = 240.0f;

std::int64_t nowUnix()
{
    return static_cast<std::int64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string elideText(const std::string& text, float maxWidth)
{
    if (text.empty() || maxWidth <= 0.0f || ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
    }
    std::string value = text;
    constexpr const char* ellipsis = "...";
    const float ellipsisWidth = ImGui::CalcTextSize(ellipsis).x;
    while (!value.empty() && ImGui::CalcTextSize(value.c_str()).x + ellipsisWidth > maxWidth) {
        if ((static_cast<unsigned char>(value.back()) & 0x80) == 0) {
            value.pop_back();
        } else {
            while (!value.empty() && (static_cast<unsigned char>(value.back()) & 0xC0) == 0x80) {
                value.pop_back();
            }
            if (!value.empty()) {
                value.pop_back();
            }
        }
    }
    return value.empty() ? ellipsis : value + ellipsis;
}

std::filesystem::path notePath(const AppContext& context, const Note& note)
{
    return note.markdownPath.is_absolute() ? note.markdownPath : context.notes.directory() / note.markdownPath;
}

void markDirty()
{
    gNotesPanel.dirty = true;
    gNotesPanel.editedAt = ImGui::GetTime();
}

void setStatus(const std::string& message)
{
    if (message.empty()) {
        gNotesPanel.statusMessage.clear();
        gNotesPanel.statusUntil = 0.0;
        return;
    }
    gNotesPanel.statusMessage = message;
    gNotesPanel.statusUntil = ImGui::GetTime() + 2.5;
}

bool isNoteSelected(const std::string& noteId)
{
    return !noteId.empty() && gNotesPanel.selectedNoteIds.contains(noteId);
}

void clearNoteSelection()
{
    gNotesPanel.selectedNoteIds.clear();
    gNotesPanel.rangeAnchorNoteId.clear();
}

void selectOnlyNote(const std::string& noteId)
{
    gNotesPanel.selectedNoteIds.clear();
    if (!noteId.empty()) {
        gNotesPanel.selectedNoteIds.insert(noteId);
        gNotesPanel.rangeAnchorNoteId = noteId;
    } else {
        gNotesPanel.rangeAnchorNoteId.clear();
    }
}

void toggleNoteSelection(const std::string& noteId)
{
    if (noteId.empty()) {
        return;
    }
    if (gNotesPanel.selectedNoteIds.contains(noteId)) {
        gNotesPanel.selectedNoteIds.erase(noteId);
    } else {
        gNotesPanel.selectedNoteIds.insert(noteId);
    }
    gNotesPanel.rangeAnchorNoteId = noteId;
}

void captureDragNoteIds(const std::string& primaryId)
{
    gNotesPanel.dragPrimaryNoteId = primaryId;
    gNotesPanel.dragNoteIdsSnapshot.clear();
    if (primaryId.empty()) {
        return;
    }
    if (!gNotesPanel.selectedNoteIds.contains(primaryId) || gNotesPanel.selectedNoteIds.size() <= 1) {
        // Single-item drag: keep selection as-is unless this id wasn't selected.
        gNotesPanel.dragNoteIdsSnapshot = {primaryId};
        if (!gNotesPanel.selectedNoteIds.contains(primaryId)) {
            selectOnlyNote(primaryId);
        }
        return;
    }
    // Preserve multi-selection order with primary first; do not collapse selection.
    gNotesPanel.dragNoteIdsSnapshot.push_back(primaryId);
    for (const std::string& id : gNotesPanel.selectedNoteIds) {
        if (id != primaryId) {
            gNotesPanel.dragNoteIdsSnapshot.push_back(id);
        }
    }
}

std::vector<std::string> activeDragNoteIds()
{
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    if (payload && payload->IsDataType(drag_payload::kNoteId)) {
        if (!gNotesPanel.dragNoteIdsSnapshot.empty()) {
            return gNotesPanel.dragNoteIdsSnapshot;
        }
        if (payload->Data && payload->DataSize > 0) {
            return {static_cast<const char*>(payload->Data)};
        }
        return {};
    }

    // Payload is cleared on some delivery frames; keep the snapshot for one more use.
    if (!gNotesPanel.dragNoteIdsSnapshot.empty()) {
        return gNotesPanel.dragNoteIdsSnapshot;
    }
    gNotesPanel.dragPrimaryNoteId.clear();
    gNotesPanel.dragPreviewAnchor.reset();
    return {};
}

std::vector<std::string> movableNoteIds(AppContext& context, const std::vector<std::string>& ids)
{
    std::vector<std::string> filtered;
    filtered.reserve(ids.size());
    std::unordered_set<std::string> seen;
    for (const std::string& id : ids) {
        const Note* note = context.notes.find(id);
        if (note && !note->fixed && seen.insert(id).second) {
            filtered.push_back(id);
        }
    }
    return filtered;
}

void clearDragNoteIdsSnapshot()
{
    gNotesPanel.dragPrimaryNoteId.clear();
    gNotesPanel.dragNoteIdsSnapshot.clear();
    gNotesPanel.dragPreviewAnchor.reset();
}

void rememberTreeRowPosition(const std::string& key, const ImVec2& pos)
{
    if (!key.empty()) {
        gNotesPanel.lastTreeRowPositions[key] = pos;
    }
}

void applyPendingTreeDropStart(const std::string& key, ImGuiID layoutId)
{
    const auto it = gNotesPanel.pendingDropPositions.find(key);
    if (it == gNotesPanel.pendingDropPositions.end()) {
        return;
    }
    ui_anim::snapLayoutPos(layoutId, it->second);
    gNotesPanel.pendingDropPositions.erase(it);
}

void storePendingFromDragPreview(const std::vector<std::string>& ids)
{
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    ImVec2 anchor(mouse.x + 12.0f, mouse.y + 10.0f);
    if (gNotesPanel.dragPreviewAnchor.has_value()) {
        anchor = *gNotesPanel.dragPreviewAnchor;
    }
    for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
        gNotesPanel.pendingDropPositions[ids[static_cast<size_t>(i)]] =
            ImVec2(anchor.x + static_cast<float>(std::min(i, 2)) * 10.0f, anchor.y + static_cast<float>(std::min(i, 2)) * 6.0f);
    }
}

ImVec2 smoothTreeDragAnchor(const ImVec2& target)
{
    if (!ui_anim::enabled()) {
        gNotesPanel.dragPreviewAnchor = target;
        return target;
    }
    if (!gNotesPanel.dragPreviewAnchor.has_value()) {
        gNotesPanel.dragPreviewAnchor = target;
        return target;
    }
    const ImVec2 current = *gNotesPanel.dragPreviewAnchor;
    const ImVec2 delta(target.x - current.x, target.y - current.y);
    const float distanceSq = delta.x * delta.x + delta.y * delta.y;
    if (distanceSq <= 0.04f) {
        gNotesPanel.dragPreviewAnchor = target;
        return target;
    }
    const float distance = std::sqrt(distanceSq);
    const float dt = std::clamp(ui_anim::dt(), 1.0f / 240.0f, 1.0f / 30.0f);
    const float followBoost = std::clamp((distance - 10.0f) / 120.0f, 0.0f, 1.0f);
    const float followRate = 24.0f + followBoost * 48.0f;
    const float alpha = 1.0f - std::exp(-followRate * dt);
    ImVec2 next(current.x + delta.x * alpha, current.y + delta.y * alpha);
    constexpr float kMaxLag = 36.0f;
    const ImVec2 remaining(target.x - next.x, target.y - next.y);
    const float remainingSq = remaining.x * remaining.x + remaining.y * remaining.y;
    if (remainingSq > kMaxLag * kMaxLag) {
        const float scale = kMaxLag / std::sqrt(remainingSq);
        next = ImVec2(target.x - remaining.x * scale, target.y - remaining.y * scale);
    }
    gNotesPanel.dragPreviewAnchor = next;
    return next;
}

void drawNoteTreeGhostSlot(const UiPalette& theme, const ImVec2& pos, float width)
{
    const float alpha = ui_anim::ghostAmount(ImGui::GetID("note-tree-ghost"));
    ImVec4 fill = theme.headerHovered;
    fill.w *= 0.22f * alpha;
    ImVec4 border = theme.frameActive;
    border.w *= alpha;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + kTreeRowHeight), ImGui::ColorConvertFloat4ToU32(fill), theme.itemRounding);
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + kTreeRowHeight), ImGui::ColorConvertFloat4ToU32(border), theme.itemRounding, 0, 1.5f);
}

void drawFloatingNoteDragPreview(const UiPalette& theme, const std::vector<std::string>& ids, const AppContext& context)
{
    if (ids.empty()) {
        return;
    }
    const ImVec2 target(ImGui::GetIO().MousePos.x + 14.0f, ImGui::GetIO().MousePos.y + 12.0f);
    const ImVec2 anchor = smoothTreeDragAnchor(target);
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const int layers = std::max(1, std::min(static_cast<int>(ids.size()), 3));
    std::string label;
    if (ids.size() > 1) {
        label = std::to_string(ids.size()) + " items";
    } else if (const Note* note = context.notes.find(ids.front())) {
        label = NotesStore::displayTitle(*note);
    } else {
        label = ids.front();
    }
    for (int i = layers - 1; i >= 0; --i) {
        const ImVec2 pos(anchor.x + static_cast<float>(i) * 10.0f, anchor.y + static_cast<float>(i) * 6.0f);
        ImVec4 fill = theme.childBg;
        fill.w = 0.92f;
        dl->AddRectFilled(pos, ImVec2(pos.x + 168.0f, pos.y + 28.0f), ImGui::ColorConvertFloat4ToU32(fill), theme.itemRounding);
        dl->AddRect(pos, ImVec2(pos.x + 168.0f, pos.y + 28.0f), ImGui::ColorConvertFloat4ToU32(theme.border), theme.itemRounding, 0, 1.2f);
        if (i == 0) {
            dl->AddText(ImVec2(pos.x + 10.0f, pos.y + 6.0f), theme.text, label.c_str());
        }
    }
}

void trimNotesWorkingSet()
{
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void clearNotesPanelState()
{
    gNotesPanel = NotesPanelState{};
}

void loadDraft(const Note& note)
{
    gNotesPanel.loadedNoteId = note.id;
    gNotesPanel.draftTitle = NotesStore::displayTitle(note);
    gNotesPanel.draftTags = formatNoteTags(note.tags);
    gNotesPanel.draftBody = note.body;
    gNotesPanel.draftPinned = note.pinned;
    gNotesPanel.draftFixed = note.fixed;
    gNotesPanel.dirty = false;
    gNotesPanel.mode = 2;
    gNotesPanel.outline.pendingHeadingIndex = -1;
    gNotesPanel.outline.pendingLine = -1;
    gNotesPanel.outline.editorScrollFrames = 0;
    gNotesPanel.selectedFolder = note.folder;
}

// Never auto-save: only explicit Save / Ctrl+S / confirm dialogs may force-save.
// When leaving a dirty note, ask first.
bool tryActivateNote(AppContext& context, const Note& note, bool openQuick = false)
{
    if (gNotesPanel.loadedNoteId == note.id) {
        context.runtime().selectedNoteId = note.id;
        gNotesPanel.selectedFolder = note.folder;
        selectOnlyNote(note.id);
        if (openQuick) {
            gNotesPanel.mode = 2;
            context.runtime().showNotes = false;
            context.runtime().showNoteQuick = true;
        }
        return true;
    }
    if (gNotesPanel.dirty) {
        gNotesPanel.switchConfirmOpen = true;
        gNotesPanel.pendingSwitchNoteId = note.id;
        gNotesPanel.closeConfirmIsQuick = openQuick;
        setStatus(tr("Unsaved changes"));
        return false;
    }
    selectOnlyNote(note.id);
    context.runtime().selectedNoteId = note.id;
    gNotesPanel.selectedFolder = note.folder;
    loadDraft(note);
    if (openQuick) {
        gNotesPanel.mode = 2;
        context.runtime().showNotes = false;
        context.runtime().showNoteQuick = true;
    }
    return true;
}

bool saveDraft(AppContext& context, bool force)
{
    if (gNotesPanel.loadedNoteId.empty() || !gNotesPanel.dirty) {
        return false;
    }
    if (!force && (ImGui::GetTime() - gNotesPanel.editedAt) < 0.65) {
        return false;
    }
    Note* note = context.notes.find(gNotesPanel.loadedNoteId);
    if (!note) {
        gNotesPanel.dirty = false;
        return false;
    }
    note->title = gNotesPanel.draftTitle.empty() ? NotesStore::displayTitle(*note) : gNotesPanel.draftTitle;
    note->tags = parseNoteTags(gNotesPanel.draftTags);
    note->body = gNotesPanel.draftBody;
    note->pinned = gNotesPanel.draftPinned;
    note->fixed = gNotesPanel.draftFixed;
    note->updatedAt = nowUnix();
    context.notes.saveNote(*note);
    context.rebuildSearch();
    gNotesPanel.dirty = false;
    return true;
}

void setNotePinned(AppContext& context, Note& note, bool pinned)
{
    const bool fixed = pinned ? false : note.fixed;
    if (note.pinned == pinned && note.fixed == fixed && gNotesPanel.draftPinned == pinned && gNotesPanel.draftFixed == fixed) {
        return;
    }
    note.pinned = pinned;
    note.fixed = fixed;
    gNotesPanel.draftPinned = pinned;
    gNotesPanel.draftFixed = fixed;
    note.updatedAt = nowUnix();
    context.notes.saveIndex();
    context.rebuildSearch();
}

void setNoteFixed(AppContext& context, Note& note, bool fixed)
{
    const bool pinned = fixed ? false : note.pinned;
    if (note.fixed == fixed && note.pinned == pinned && gNotesPanel.draftFixed == fixed && gNotesPanel.draftPinned == pinned) {
        return;
    }
    note.pinned = pinned;
    note.fixed = fixed;
    gNotesPanel.draftPinned = pinned;
    gNotesPanel.draftFixed = fixed;
    note.updatedAt = nowUnix();
    context.notes.saveIndex();
    context.rebuildSearch();
}

void ensureSelection(AppContext& context)
{
    if (!context.runtime().selectedNoteId.empty() && context.notes.find(context.runtime().selectedNoteId)) {
        return;
    }
    context.runtime().selectedNoteId.clear();
    for (const Note& note : context.notes.notes()) {
        if (!note.archived) {
            context.runtime().selectedNoteId = note.id;
            return;
        }
    }
    if (!context.notes.notes().empty()) {
        context.runtime().selectedNoteId = context.notes.notes().front().id;
    }
}

void deleteNotesByIds(AppContext& context, const std::vector<std::string>& ids)
{
    if (ids.empty()) {
        return;
    }
    saveDraft(context, true);
    int removed = 0;
    for (const std::string& id : ids) {
        if (context.notes.removeNoteFile(id)) {
            ++removed;
            gNotesPanel.selectedNoteIds.erase(id);
            if (gNotesPanel.loadedNoteId == id) {
                gNotesPanel.loadedNoteId.clear();
                gNotesPanel.draftTitle.clear();
                gNotesPanel.draftTags.clear();
                gNotesPanel.draftBody.clear();
                gNotesPanel.dirty = false;
            }
            if (context.runtime().selectedNoteId == id) {
                context.runtime().selectedNoteId.clear();
            }
            if (gNotesPanel.rangeAnchorNoteId == id) {
                gNotesPanel.rangeAnchorNoteId.clear();
            }
            if (gNotesPanel.pressNoteId == id) {
                gNotesPanel.pressNoteId.clear();
            }
        }
    }
    if (removed <= 0) {
        setStatus(tr("Delete failed"));
        return;
    }
    context.rebuildSearch();
    ensureSelection(context);
    if (Note* next = context.notes.find(context.runtime().selectedNoteId)) {
        loadDraft(*next);
    }
    setStatus(removed == 1 ? tr("Note deleted") : tr("Notes deleted"));
}

void expandFolderAncestors(const std::string& folderPath);

bool noteClipboardAvailable()
{
    return !gNotesPanel.noteClipboard.empty();
}

void copyNotesToClipboard(AppContext& context, const std::vector<std::string>& ids, bool cut)
{
    gNotesPanel.noteClipboard.clear();
    gNotesPanel.noteClipboardCut = cut;
    for (const std::string& id : ids) {
        if (const Note* note = context.notes.find(id)) {
            gNotesPanel.noteClipboard.push_back(*note);
        }
    }
    if (gNotesPanel.noteClipboard.empty()) {
        setStatus(tr("Copy failed"));
        return;
    }
    setStatus(cut ? tr("Cut") : tr("Copied"));
}

void pasteNotesFromClipboard(AppContext& context, const std::string& folder)
{
    if (gNotesPanel.noteClipboard.empty()) {
        return;
    }
    saveDraft(context, true);
    std::string targetFolder = NotesStore::normalizeFolderPath(folder);
    if (targetFolder.empty()) {
        targetFolder = gNotesPanel.selectedFolder;
    }
    std::vector<std::string> createdIds;
    createdIds.reserve(gNotesPanel.noteClipboard.size());
    for (const Note& src : gNotesPanel.noteClipboard) {
        Note& created = context.notes.createNote(src.title.empty() ? tr("Untitled Note") : src.title, targetFolder);
        created.tags = src.tags;
        created.pinned = src.pinned;
        created.fixed = src.fixed;
        created.body = src.body;
        created.updatedAt = nowUnix();
        context.notes.saveNote(created);
        createdIds.push_back(created.id);
    }
    if (gNotesPanel.noteClipboardCut) {
        std::vector<std::string> removeIds;
        removeIds.reserve(gNotesPanel.noteClipboard.size());
        for (const Note& src : gNotesPanel.noteClipboard) {
            removeIds.push_back(src.id);
        }
        deleteNotesByIds(context, removeIds);
        gNotesPanel.noteClipboard.clear();
        gNotesPanel.noteClipboardCut = false;
    }
    context.rebuildSearch();
    if (!createdIds.empty()) {
        selectOnlyNote(createdIds.front());
        context.runtime().selectedNoteId = createdIds.front();
        if (Note* note = context.notes.find(createdIds.front())) {
            loadDraft(*note);
        }
        expandFolderAncestors(targetFolder);
    }
    setStatus(tr("Pasted"));
}

std::vector<std::string> selectedOrSingleNoteIds(const std::string& noteId)
{
    std::vector<std::string> ids;
    if (isNoteSelected(noteId) && gNotesPanel.selectedNoteIds.size() > 1) {
        ids.assign(gNotesPanel.selectedNoteIds.begin(), gNotesPanel.selectedNoteIds.end());
    } else if (!noteId.empty()) {
        ids.push_back(noteId);
    }
    return ids;
}

bool requestCloseNotes(AppContext& context, bool quick)
{
    if (gNotesPanel.dirty) {
        gNotesPanel.closeConfirmOpen = true;
        gNotesPanel.closeConfirmIsQuick = quick;
        return false;
    }
    if (quick) {
        context.runtime().showNoteQuick = false;
    } else {
        context.runtime().showNotes = false;
    }
    return true;
}

void drawCloseConfirmPopup(AppContext& context, const UiPalette& theme)
{
    auto drawOne = [&](bool& openFlag, const char* classId, const char* winId, const char* message, auto onSave, auto onDiscard) {
        if (!openFlag) {
            return;
        }
        setupManagedWindow(classId);
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(400.0f, 160.0f), ImGuiCond_Appearing);
        ManagedWindowStyle windowStyle(theme);
        bool open = true;
        if (ImGui::Begin(winId, &open,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoSavedSettings)) {
            applyManagedViewportChrome(ImGui::GetWindowViewport()->PlatformHandleRaw, context.themes.active(), theme);
            const float width = ImGui::GetWindowWidth();
            const float height = ImGui::GetWindowHeight();
            const ImVec2 pos = ImGui::GetWindowPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (drawManagedTitleBar(theme, tr("Unsaved Changes"), open) || !open) {
                openFlag = false;
            } else {
                ImGui::SetCursorPos(ImVec2(16.0f, kUiTitleHeight + 16.0f));
                ImGui::PushTextWrapPos(width - 32.0f);
                ImGui::TextUnformatted(message);
                ImGui::PopTextWrapPos();
                dl->AddRectFilled(
                    ImVec2(pos.x, pos.y + height - 52.0f), ImVec2(pos.x + width, pos.y + height),
                    IM_COL32(static_cast<int>(theme.childBg.x * 255.0f + 0.5f), static_cast<int>(theme.childBg.y * 255.0f + 0.5f),
                             static_cast<int>(theme.childBg.z * 255.0f + 0.5f), static_cast<int>(theme.childBg.w * 255.0f + 0.5f)));
                ImGui::SetCursorPos(ImVec2(width - 318.0f, height - 40.0f));
                if (ImGui::Button(tr("Save"), ImVec2(90.0f, 30.0f))) {
                    openFlag = false;
                    onSave();
                }
                ImGui::SameLine();
                if (ImGui::Button(tr("Don't Save"), ImVec2(100.0f, 30.0f))) {
                    openFlag = false;
                    onDiscard();
                }
                ImGui::SameLine();
                if (ImGui::Button(tr("Cancel"), ImVec2(90.0f, 30.0f))) {
                    openFlag = false;
                }
            }
        }
        ImGui::End();
    };

    drawOne(
        gNotesPanel.closeConfirmOpen, "LauncherManagedNotesUnsavedClose", "Unsaved Changes###notes-unsaved-close",
        tr("Note has unsaved changes. Save before closing?"),
        [&]() {
            markDirty();
            saveDraft(context, true);
            if (gNotesPanel.closeConfirmIsQuick) {
                context.runtime().showNoteQuick = false;
            } else {
                context.runtime().showNotes = false;
            }
        },
        [&]() {
            gNotesPanel.dirty = false;
            if (const Note* note = context.notes.find(gNotesPanel.loadedNoteId)) {
                loadDraft(*note);
            }
            if (gNotesPanel.closeConfirmIsQuick) {
                context.runtime().showNoteQuick = false;
            } else {
                context.runtime().showNotes = false;
            }
        });

    drawOne(
        gNotesPanel.switchConfirmOpen, "LauncherManagedNotesUnsavedSwitch", "Unsaved Changes###notes-unsaved-switch",
        tr("Note has unsaved changes. Save before switching?"),
        [&]() {
            markDirty();
            saveDraft(context, true);
            const std::string nextId = gNotesPanel.pendingSwitchNoteId;
            const bool openQuick = gNotesPanel.closeConfirmIsQuick;
            gNotesPanel.pendingSwitchNoteId.clear();
            gNotesPanel.closeConfirmIsQuick = false;
            if (const Note* note = context.notes.find(nextId)) {
                selectOnlyNote(note->id);
                context.runtime().selectedNoteId = note->id;
                gNotesPanel.selectedFolder = note->folder;
                loadDraft(*note);
                if (openQuick) {
                    gNotesPanel.mode = 2;
                    context.runtime().showNotes = false;
                    context.runtime().showNoteQuick = true;
                }
            }
        },
        [&]() {
            gNotesPanel.dirty = false;
            const std::string nextId = gNotesPanel.pendingSwitchNoteId;
            const bool openQuick = gNotesPanel.closeConfirmIsQuick;
            gNotesPanel.pendingSwitchNoteId.clear();
            gNotesPanel.closeConfirmIsQuick = false;
            if (const Note* note = context.notes.find(nextId)) {
                selectOnlyNote(note->id);
                context.runtime().selectedNoteId = note->id;
                gNotesPanel.selectedFolder = note->folder;
                loadDraft(*note);
                if (openQuick) {
                    gNotesPanel.mode = 2;
                    context.runtime().showNotes = false;
                    context.runtime().showNoteQuick = true;
                }
            }
        });
}

bool noteMatchesFilter(const Note& note, const std::string& filter)
{
    if (filter.empty()) {
        return true;
    }
    std::string haystack = NotesStore::displayTitle(note) + " " + formatNoteTags(note.tags) + " " + note.folder + " " + note.body;
    return lower(std::move(haystack)).find(filter) != std::string::npos;
}

bool folderExpanded(const std::string& path)
{
    const auto it = gNotesPanel.folderExpanded.find(path);
    if (it == gNotesPanel.folderExpanded.end()) {
        return true;
    }
    return it->second;
}

void setFolderExpanded(const std::string& path, bool expanded)
{
    gNotesPanel.folderExpanded[path] = expanded;
}

std::vector<std::string> visibleNoteIdsInTree(const NoteTreeNode& root)
{
    std::vector<std::string> ids;
    std::function<void(const NoteTreeNode&)> walk = [&](const NoteTreeNode& node) {
        for (const NoteTreeNode& child : node.children) {
            if (child.isFolder) {
                if (folderExpanded(child.folderPath)) {
                    walk(child);
                }
            } else if (!child.noteId.empty()) {
                ids.push_back(child.noteId);
            }
        }
    };
    walk(root);
    return ids;
}

void selectNoteRangeFromVisible(const std::vector<std::string>& visible, const std::string& toId, const std::string& preferredFromId = {})
{
    if (toId.empty()) {
        return;
    }
    if (visible.empty()) {
        selectOnlyNote(toId);
        return;
    }

    std::string fromId = gNotesPanel.rangeAnchorNoteId;
    if (fromId.empty() || std::find(visible.begin(), visible.end(), fromId) == visible.end()) {
        fromId = preferredFromId;
    }
    if (fromId.empty() || std::find(visible.begin(), visible.end(), fromId) == visible.end()) {
        fromId = toId;
    }

    int a = -1;
    int b = -1;
    for (int i = 0; i < static_cast<int>(visible.size()); ++i) {
        if (visible[static_cast<size_t>(i)] == fromId) {
            a = i;
        }
        if (visible[static_cast<size_t>(i)] == toId) {
            b = i;
        }
    }
    if (a < 0 || b < 0) {
        selectOnlyNote(toId);
        return;
    }
    if (a > b) {
        std::swap(a, b);
    }

    // Preserve anchor for chained Shift selections (Excel-like).
    if (gNotesPanel.rangeAnchorNoteId.empty() ||
        std::find(visible.begin(), visible.end(), gNotesPanel.rangeAnchorNoteId) == visible.end()) {
        gNotesPanel.rangeAnchorNoteId = fromId;
    }

    gNotesPanel.selectedNoteIds.clear();
    for (int i = a; i <= b; ++i) {
        gNotesPanel.selectedNoteIds.insert(visible[static_cast<size_t>(i)]);
    }
}

void selectNoteRange(const NoteTreeNode& root, const std::string& toId)
{
    selectNoteRangeFromVisible(visibleNoteIdsInTree(root), toId);
}

void selectNoteRangeCached(const std::string& toId, const std::string& preferredFromId = {})
{
    if (!gNotesPanel.lastVisibleNoteIds.empty()) {
        selectNoteRangeFromVisible(gNotesPanel.lastVisibleNoteIds, toId, preferredFromId);
        return;
    }
    selectOnlyNote(toId);
}

void expandFolderAncestors(const std::string& folderPath)
{
    std::string path = NotesStore::normalizeFolderPath(folderPath);
    while (!path.empty()) {
        setFolderExpanded(path, true);
        path = NotesStore::folderParent(path);
    }
}

NoteTreeNode* findOrCreateChildFolder(NoteTreeNode& parent, const std::string& name, const std::string& fullPath)
{
    for (NoteTreeNode& child : parent.children) {
        if (child.isFolder && child.name == name) {
            return &child;
        }
    }
    NoteTreeNode node;
    node.isFolder = true;
    node.name = name;
    node.folderPath = fullPath;
    parent.children.push_back(std::move(node));
    return &parent.children.back();
}

void insertNoteNode(NoteTreeNode& parent, const Note& note)
{
    NoteTreeNode node;
    node.isFolder = false;
    node.pinned = note.pinned;
    node.fixed = note.fixed;
    node.name = NotesStore::displayTitle(note);
    node.folderPath = note.folder;
    node.noteId = note.id;
    parent.children.push_back(std::move(node));
}

void finalizeTreeOrder(NoteTreeNode& node)
{
    // Display order: folders, then pinned notes, then plain notes.
    // Within each note group, preserve notes_ free order so drag-reorder sticks.
    std::vector<NoteTreeNode> folders;
    std::vector<NoteTreeNode> pinnedNotes;
    std::vector<NoteTreeNode> plainNotes;
    folders.reserve(node.children.size());
    pinnedNotes.reserve(node.children.size());
    plainNotes.reserve(node.children.size());
    for (NoteTreeNode& child : node.children) {
        if (child.isFolder) {
            finalizeTreeOrder(child);
            folders.push_back(std::move(child));
        } else if (child.pinned) {
            pinnedNotes.push_back(std::move(child));
        } else {
            plainNotes.push_back(std::move(child));
        }
    }
    node.children.clear();
    node.children.insert(node.children.end(), std::make_move_iterator(folders.begin()), std::make_move_iterator(folders.end()));
    node.children.insert(node.children.end(), std::make_move_iterator(pinnedNotes.begin()), std::make_move_iterator(pinnedNotes.end()));
    node.children.insert(node.children.end(), std::make_move_iterator(plainNotes.begin()), std::make_move_iterator(plainNotes.end()));
}

NoteTreeNode buildNoteTree(const AppContext& context)
{
    NoteTreeNode root;
    root.isFolder = true;
    root.name = tr("Notes");
    root.folderPath.clear();

    const std::string filter = lower(gNotesPanel.filter);
    const bool filtering = !filter.empty();
    std::unordered_set<std::string> foldersWithMatches;

    for (const std::string& folder : context.notes.folders()) {
        NoteTreeNode* current = &root;
        std::string built;
        size_t start = 0;
        while (start <= folder.size()) {
            const size_t slash = folder.find('/', start);
            const std::string segment = folder.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            if (!segment.empty()) {
                if (!built.empty()) {
                    built.push_back('/');
                }
                built += segment;
                current = findOrCreateChildFolder(*current, segment, built);
            }
            if (slash == std::string::npos) {
                break;
            }
            start = slash + 1;
        }
    }

    const std::vector<Note>& notes = context.notes.notes();
    std::vector<const Note*> visible;
    for (const Note& note : notes) {
        const bool archiveVisible = gNotesPanel.includeArchived || !note.archived;
        if (!archiveVisible || !noteMatchesFilter(note, filter)) {
            continue;
        }
        visible.push_back(&note);
        if (filtering) {
            std::string path = note.folder;
            while (!path.empty()) {
                foldersWithMatches.insert(path);
                path = NotesStore::folderParent(path);
            }
        }
    }

    // Preserve notes_ order for drag reorder; finalizeTreeOrder groups folders / pinned / plain.
    for (const Note* note : visible) {
        NoteTreeNode* current = &root;
        if (!note->folder.empty()) {
            std::string built;
            size_t start = 0;
            while (start <= note->folder.size()) {
                const size_t slash = note->folder.find('/', start);
                const std::string segment = note->folder.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                if (!segment.empty()) {
                    if (!built.empty()) {
                        built.push_back('/');
                    }
                    built += segment;
                    current = findOrCreateChildFolder(*current, segment, built);
                }
                if (slash == std::string::npos) {
                    break;
                }
                start = slash + 1;
            }
        }
        insertNoteNode(*current, *note);
    }

    if (filtering) {
        std::function<void(NoteTreeNode&)> prune = [&](NoteTreeNode& node) {
            node.children.erase(std::remove_if(node.children.begin(), node.children.end(),
                                               [&](NoteTreeNode& child) {
                                                   if (!child.isFolder) {
                                                       return false;
                                                   }
                                                   prune(child);
                                                   const bool keep =
                                                       foldersWithMatches.contains(child.folderPath) || !child.children.empty();
                                                   return !keep;
                                               }),
                                node.children.end());
        };
        prune(root);
        for (const std::string& path : foldersWithMatches) {
            setFolderExpanded(path, true);
        }
    }

    finalizeTreeOrder(root);
    return root;
}

void openNoteExternally(const AppContext& context, const Note& note)
{
    const std::filesystem::path path = notePath(context, note);
    ShellExecuteW(nullptr, L"open", path.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

ImVec4 colorWithAlpha(ImU32 color, float alpha)
{
    ImVec4 result = ImGui::ColorConvertU32ToFloat4(color);
    result.w *= alpha;
    return result;
}

ImVec4 colorWithAlpha(ImVec4 color, float alpha)
{
    color.w *= alpha;
    return color;
}

ImVec4 mixVec4(ImVec4 base, ImVec4 target, float amount)
{
    amount = std::clamp(amount, 0.0f, 1.0f);
    return ImVec4(base.x + (target.x - base.x) * amount, base.y + (target.y - base.y) * amount, base.z + (target.z - base.z) * amount,
                  base.w + (target.w - base.w) * amount);
}

ImU32 mixColor(ImVec4 base, ImVec4 target, float amount)
{
    return ImGui::ColorConvertFloat4ToU32(mixVec4(base, target, amount));
}

std::vector<LaunchItem>* currentItemList(AppContext& context)
{
    RuntimeState& runtime = context.runtime();
    PersistedState& persisted = context.persisted();
    if (runtime.selectedCategory < 0 || runtime.selectedCategory >= static_cast<int>(persisted.categories.size())) {
        return nullptr;
    }
    std::vector<LaunchItem>* items = &persisted.categories[static_cast<size_t>(runtime.selectedCategory)].items;
    for (const std::string& folderId : runtime.currentFolderStack) {
        auto it = std::find_if(items->begin(), items->end(), [&](const LaunchItem& item) {
            return item.id == folderId && item.type == LaunchItemType::VirtualFolder;
        });
        if (it == items->end()) {
            return items;
        }
        items = &it->children;
    }
    return items;
}

bool currentListHasNoteItem(AppContext& context, const Note& note)
{
    const std::vector<LaunchItem>* items = currentItemList(context);
    if (!items) {
        return false;
    }
    return std::any_of(items->begin(), items->end(), [&](const LaunchItem& item) {
        return item.type == LaunchItemType::Note && item.target.string() == note.id;
    });
}

void addNoteItemToCurrentList(AppContext& context, const Note& note)
{
    std::vector<LaunchItem>* items = currentItemList(context);
    if (!items || currentListHasNoteItem(context, note)) {
        return;
    }
    const std::int64_t timestamp = nowUnix();
    LaunchItem item;
    item.id = "note-item-" + note.id + "-" + std::to_string(timestamp);
    item.name = NotesStore::displayTitle(note);
    item.subtitle = formatNoteTags(note.tags).empty() ? tr("Notes") : formatNoteTags(note.tags);
    item.target = note.id;
    item.type = LaunchItemType::Note;
    item.fallbackColor = "#6A9A7CFF";
    item.createdAt = timestamp;
    item.lastEditedAt = timestamp;
    items->push_back(std::move(item));
    context.commitContentChange();
}

bool addNoteIdsAsListItemsImpl(AppContext& context, const std::vector<std::string>& noteIds)
{
    int added = 0;
    for (const std::string& id : noteIds) {
        if (const Note* note = context.notes.find(id)) {
            if (!currentListHasNoteItem(context, *note)) {
                addNoteItemToCurrentList(context, *note);
                ++added;
            }
        }
    }
    return added > 0;
}

bool notesButton(const UiPalette& theme, const char* icon, const char* label, bool active = false, ImVec2 size = ImVec2(0.0f, 0.0f))
{
    ImGui::PushID(label);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const ImGuiStyle& style = ImGui::GetStyle();
    const std::string text = icon && icon[0] ? std::string(icon) + "  " + label : std::string(label);
    const ImVec2 textSize = ImGui::CalcTextSize(text.c_str());
    if (size.x <= 0.0f) {
        size.x = textSize.x + style.FramePadding.x * 2.0f + 6.0f;
    }
    if (size.y <= 0.0f) {
        size.y = 32.0f;
    }
    ImGui::InvisibleButton("button", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool clicked = ImGui::IsItemClicked();
    const float hoverT = ui_anim::hoverAmount(ImGui::GetID("hover"), hovered || active, 0.12f);
    const ImU32 bg = active ? ImGui::ColorConvertFloat4ToU32(theme.headerActive) : mixColor(theme.button, theme.buttonHovered, hoverT);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, theme.frameRounding);
    if (hoverT > 0.01f || active) {
        dl->AddRect(pos, ImVec2(pos.x + size.x, pos.y + size.y), ImGui::ColorConvertFloat4ToU32(theme.border), theme.frameRounding, 0,
                    1.0f);
    }
    dl->AddText(ImVec2(pos.x + (size.x - textSize.x) * 0.5f, pos.y + (size.y - textSize.y) * 0.5f), theme.text, text.c_str());
    ui_anim::rippleLastItem(theme, theme.frameRounding);
    ImGui::PopID();
    return clicked;
}

void drawNoteStateButtons(AppContext& context, const UiPalette& theme, Note* selected)
{
    ImGui::BeginDisabled(!selected);
    if (notesButton(theme, Icons::TopMost, tr("Pinned"), selected && gNotesPanel.draftPinned) && selected) {
        setNotePinned(context, *selected, !gNotesPanel.draftPinned);
    }
    ImGui::SameLine();
    if (notesButton(theme, Icons::Pin, tr("Fixed"), selected && gNotesPanel.draftFixed) && selected) {
        setNoteFixed(context, *selected, !gNotesPanel.draftFixed);
    }
    ImGui::EndDisabled();
}

void pushPaneStyle(const UiPalette& theme)
{
    // Flat panes: transparent child bg, no border boxes. Columns are separated by lines only.
    (void)theme;
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.0f, 10.0f));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
}

void popPaneStyle()
{
    ImGui::PopStyleColor(1);
    ImGui::PopStyleVar(3);
}

void drawVerticalPaneSeparator(const UiPalette& theme, float height)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float x = pos.x + 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(x, pos.y + 4.0f), ImVec2(x, pos.y + height - 4.0f),
                ImGui::ColorConvertFloat4ToU32(colorWithAlpha(theme.border, 0.55f)), 1.0f);
    ImGui::Dummy(ImVec2(1.0f, height));
}

void drawHorizontalPaneSeparator(const UiPalette& theme, float width)
{
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float y = pos.y + 0.5f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(pos.x + 4.0f, y), ImVec2(pos.x + width - 4.0f, y),
                ImGui::ColorConvertFloat4ToU32(colorWithAlpha(theme.border, 0.55f)), 1.0f);
    ImGui::Dummy(ImVec2(width, 1.0f));
}

void createNoteInFolder(AppContext& context, const std::string& folder)
{
    saveDraft(context, true);
    Note& note = context.notes.createNote(tr("Untitled Note"), folder);
    context.runtime().selectedNoteId = note.id;
    gNotesPanel.selectedFolder = note.folder;
    expandFolderAncestors(note.folder);
    loadDraft(note);
    gNotesPanel.mode = 0;
    context.rebuildSearch();
}

void openRenamePopup(bool isFolder, const std::string& target, const std::string& draft)
{
    gNotesPanel.renameOpen = true;
    gNotesPanel.renameIsFolder = isFolder;
    gNotesPanel.renameTarget = target;
    gNotesPanel.renameDraft = draft;
}

void openNewFolderPopup(const std::string& parent)
{
    gNotesPanel.newFolderOpen = true;
    gNotesPanel.newFolderParent = parent;
    gNotesPanel.newFolderDraft.clear();
}

std::string trimCopy(std::string value)
{
    auto notSpace = [](unsigned char ch) {
        return !std::isspace(ch);
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

void applyRename(AppContext& context)
{
    const std::string draft = NotesStore::normalizeFolderPath(gNotesPanel.renameDraft);
    if (gNotesPanel.renameIsFolder) {
        const std::string parent = NotesStore::folderParent(gNotesPanel.renameTarget);
        const std::string name = NotesStore::folderName(draft.empty() ? gNotesPanel.renameDraft : draft);
        if (name.empty()) {
            setStatus(tr("Invalid folder name"));
            return;
        }
        const std::string next = parent.empty() ? name : parent + "/" + name;
        std::string error;
        if (!context.notes.renameFolder(gNotesPanel.renameTarget, next, &error)) {
            setStatus(error.empty() ? tr("Rename failed") : error);
            return;
        }
        if (gNotesPanel.selectedFolder == gNotesPanel.renameTarget ||
            NotesStore::isFolderAncestor(gNotesPanel.renameTarget, gNotesPanel.selectedFolder)) {
            const std::string suffix = gNotesPanel.selectedFolder == gNotesPanel.renameTarget
                                           ? std::string{}
                                           : gNotesPanel.selectedFolder.substr(gNotesPanel.renameTarget.size());
            gNotesPanel.selectedFolder = next + suffix;
        }
        expandFolderAncestors(next);
        setStatus(tr("Folder renamed"));
    } else if (Note* note = context.notes.find(gNotesPanel.renameTarget)) {
        const std::string title = trimCopy(gNotesPanel.renameDraft);
        note->title = title.empty() ? tr("Untitled Note") : title;
        note->updatedAt = nowUnix();
        if (gNotesPanel.loadedNoteId == note->id) {
            gNotesPanel.draftTitle = note->title;
        }
        context.notes.saveIndex();
        context.rebuildSearch();
        setStatus(tr("Note renamed"));
    }
    gNotesPanel.renameOpen = false;
}

void applyNewFolder(AppContext& context)
{
    const std::string name = NotesStore::normalizeFolderPath(gNotesPanel.newFolderDraft);
    const std::string leaf = NotesStore::folderName(name.empty() ? gNotesPanel.newFolderDraft : name);
    if (leaf.empty()) {
        setStatus(tr("Invalid folder name"));
        return;
    }
    const std::string path = gNotesPanel.newFolderParent.empty() ? leaf : gNotesPanel.newFolderParent + "/" + leaf;
    std::string error;
    if (!context.notes.createFolder(path, &error)) {
        setStatus(error.empty() ? tr("Create folder failed") : error);
        return;
    }
    gNotesPanel.selectedFolder = path;
    expandFolderAncestors(path);
    gNotesPanel.newFolderOpen = false;
    setStatus(tr("Folder created"));
}

void drawRenamePopup(AppContext& context, const UiPalette& theme)
{
    if (!gNotesPanel.renameOpen) {
        return;
    }
    setupManagedWindow("LauncherManagedNotesRename");
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380.0f, 170.0f), ImGuiCond_Appearing);
    ManagedWindowStyle windowStyle(theme);
    bool open = true;
    if (!ImGui::Begin("###notes-rename-window", &open,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoSavedSettings)) {
        gNotesPanel.renameOpen = open;
        ImGui::End();
        return;
    }
    applyManagedViewportChrome(ImGui::GetWindowViewport()->PlatformHandleRaw, context.themes.active(), theme);
    if (!open) {
        gNotesPanel.renameOpen = false;
        ImGui::End();
        return;
    }
    drawManagedTitleBar(theme, gNotesPanel.renameIsFolder ? tr("Rename Folder") : tr("Rename Note"), open);
    if (!open) {
        gNotesPanel.renameOpen = false;
        ImGui::End();
        return;
    }
    ImGui::SetCursorPos(ImVec2(16.0f, kUiTitleHeight + 14.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, colorWithAlpha(theme.frameBg, 0.95f));
    ImGui::SetNextItemWidth(-16.0f);
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    if (ImGui::InputTextWithHint("##rename-draft", tr("Name"), &gNotesPanel.renameDraft, ImGuiInputTextFlags_EnterReturnsTrue)) {
        applyRename(context);
    }
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 210.0f, ImGui::GetWindowHeight() - 46.0f));
    if (notesButton(theme, nullptr, tr("OK"), false, ImVec2(90.0f, 30.0f))) {
        applyRename(context);
    }
    ImGui::SameLine();
    if (notesButton(theme, nullptr, tr("Cancel"), false, ImVec2(90.0f, 30.0f))) {
        gNotesPanel.renameOpen = false;
    }
    ImGui::End();
}

void drawNewFolderPopup(AppContext& context, const UiPalette& theme)
{
    if (!gNotesPanel.newFolderOpen) {
        return;
    }
    setupManagedWindow("LauncherManagedNotesNewFolder");
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380.0f, 190.0f), ImGuiCond_Appearing);
    ManagedWindowStyle windowStyle(theme);
    bool open = true;
    if (!ImGui::Begin("###notes-new-folder-window", &open,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoSavedSettings)) {
        gNotesPanel.newFolderOpen = open;
        ImGui::End();
        return;
    }
    applyManagedViewportChrome(ImGui::GetWindowViewport()->PlatformHandleRaw, context.themes.active(), theme);
    if (!open) {
        gNotesPanel.newFolderOpen = false;
        ImGui::End();
        return;
    }
    drawManagedTitleBar(theme, tr("New Folder"), open);
    if (!open) {
        gNotesPanel.newFolderOpen = false;
        ImGui::End();
        return;
    }
    ImGui::SetCursorPos(ImVec2(16.0f, kUiTitleHeight + 14.0f));
    if (!gNotesPanel.newFolderParent.empty()) {
        ImGui::TextDisabled("%s: %s", tr("Parent"), gNotesPanel.newFolderParent.c_str());
        ImGui::Dummy(ImVec2(1.0f, 6.0f));
    }
    ImGui::PushStyleColor(ImGuiCol_FrameBg, colorWithAlpha(theme.frameBg, 0.95f));
    ImGui::SetNextItemWidth(-16.0f);
    if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
    }
    if (ImGui::InputTextWithHint("##new-folder-draft", tr("Folder name"), &gNotesPanel.newFolderDraft,
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
        applyNewFolder(context);
    }
    ImGui::PopStyleColor();
    ImGui::SetCursorPos(ImVec2(ImGui::GetWindowWidth() - 210.0f, ImGui::GetWindowHeight() - 46.0f));
    if (notesButton(theme, nullptr, tr("OK"), false, ImVec2(90.0f, 30.0f))) {
        applyNewFolder(context);
    }
    ImGui::SameLine();
    if (notesButton(theme, nullptr, tr("Cancel"), false, ImVec2(90.0f, 30.0f))) {
        gNotesPanel.newFolderOpen = false;
    }
    ImGui::End();
}

void drawNotesToolbar(AppContext& context, const UiPalette& theme, Note* selected)
{
    if (notesButton(theme, Icons::Add, tr("New Note"))) {
        createNoteInFolder(context, gNotesPanel.selectedFolder);
    }
    ImGui::SameLine();
    if (notesButton(theme, Icons::Folder, tr("New Folder"))) {
        openNewFolderPopup(gNotesPanel.selectedFolder);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!selected);
    if (notesButton(theme, Icons::CopyProperties, tr("Save")) && selected) {
        markDirty();
        saveDraft(context, true);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!selected);
    if (notesButton(theme, Icons::Note, tr("Add to List")) && selected) {
        saveDraft(context, true);
        if (currentListHasNoteItem(context, *selected)) {
            setStatus(tr("Already in list"));
        } else {
            addNoteItemToCurrentList(context, *selected);
            setStatus(tr("Added to list"));
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!selected);
    if (notesButton(theme, Icons::OpenWith, tr("Open File")) && selected) {
        saveDraft(context, true);
        openNoteExternally(context, *selected);
    }
    ImGui::SameLine();
    if (notesButton(theme, selected && selected->archived ? Icons::Refresh : Icons::Delete,
                    selected && selected->archived ? tr("Restore") : tr("Archive")) &&
        selected) {
        context.notes.archiveNote(selected->id, !selected->archived);
        context.rebuildSearch();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (notesButton(theme, Icons::Layout, tr("Outline"), gNotesPanel.outline.showOutline)) {
        gNotesPanel.outline.showOutline = !gNotesPanel.outline.showOutline;
    }
    ImGui::SameLine();
    drawNoteStateButtons(context, theme, selected);
}

struct TreeRowResult {
    ImRect rect{};
    bool hovered = false;
    bool clicked = false;
    bool doubleClicked = false;
    bool active = false;
};

TreeRowResult drawTreeRow(const UiPalette& theme, const char* icon, const std::string& label, bool selected, bool muted, float depth,
                          bool dropHighlight = false, const char* layoutKey = nullptr, bool hideContent = false)
{
    TreeRowResult result;
    const ImVec2 logical = ImGui::GetCursorScreenPos();
    const float rowWidth = ImGui::GetContentRegionAvail().x;
    ImVec2 visual = logical;
    if (layoutKey && layoutKey[0]) {
        const ImGuiID layoutId = ImGui::GetID(layoutKey);
        applyPendingTreeDropStart(layoutKey, layoutId);
        // Folder expand/collapse changes many row Y positions at once. Animating them
        // makes the right editor area look like it flickers. Only animate free note rows
        // while a note drag is active; otherwise snap layout immediately.
        const bool noteDragActive = ImGui::GetDragDropPayload() && ImGui::GetDragDropPayload()->IsDataType(drag_payload::kNoteId);
        const bool isFolderKey = std::string_view(layoutKey).rfind("folder:", 0) == 0;
        if (noteDragActive && !isFolderKey) {
            visual = ui_anim::layoutPos(layoutId, logical, 0.16f);
        } else {
            ui_anim::snapLayoutPos(layoutId, logical);
            visual = logical;
        }
        rememberTreeRowPosition(layoutKey, visual);
    }
    if (hideContent) {
        ImGui::Dummy(ImVec2(rowWidth, kTreeRowHeight));
        result.rect = ImRect(logical, ImVec2(logical.x + rowWidth, logical.y + kTreeRowHeight));
        return result;
    }
    ImGui::SetCursorScreenPos(visual);
    result.rect = ImRect(visual, ImVec2(visual.x + rowWidth, visual.y + kTreeRowHeight));
    ImGui::InvisibleButton("tree-row", ImVec2(rowWidth, kTreeRowHeight));
    ImGui::SetCursorScreenPos(ImVec2(logical.x, logical.y + kTreeRowHeight));
    result.hovered = ImGui::IsItemHovered();
    result.clicked = ImGui::IsItemClicked();
    result.doubleClicked = result.hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
    result.active = ImGui::IsItemActive();
    const float hoverT = ui_anim::hoverAmount(ImGui::GetID("tree-row-hover"), result.hovered || selected || dropHighlight, 0.12f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hoverT > 0.01f || selected || dropHighlight) {
        const ImU32 fill = dropHighlight ? ImGui::ColorConvertFloat4ToU32(theme.frameActive)
                                         : (selected ? ImGui::ColorConvertFloat4ToU32(theme.headerActive)
                                                     : mixColor(theme.childBg, theme.headerHovered, hoverT));
        dl->AddRectFilled(result.rect.Min, result.rect.Max, fill, theme.itemRounding);
        if (dropHighlight) {
            dl->AddRect(result.rect.Min, result.rect.Max, ImGui::ColorConvertFloat4ToU32(theme.border), theme.itemRounding, 0, 1.5f);
        }
    }
    ui_anim::rippleLastItem(theme, theme.itemRounding);
    const float indent = 8.0f + depth * 14.0f;
    const ImU32 textColor = muted ? theme.textMuted : theme.text;
    if (icon && icon[0]) {
        dl->AddText(ImVec2(visual.x + indent, visual.y + 5.0f), textColor, icon);
    }
    const float textX = visual.x + indent + 22.0f;
    const std::string visible = elideText(label, std::max(24.0f, rowWidth - indent - 28.0f));
    dl->AddText(ImVec2(textX, visual.y + 5.0f), textColor, visible.c_str());
    return result;
}

int localIndexAmongNotes(AppContext& context, const std::string& folder, const std::string& noteId)
{
    int index = 0;
    for (const Note& note : context.notes.notes()) {
        if (note.folder != folder) {
            continue;
        }
        if (note.id == noteId) {
            return index;
        }
        ++index;
    }
    return -1;
}

int localIndexAmongFolders(AppContext& context, const std::string& parent, const std::string& folderPath)
{
    int index = 0;
    for (const std::string& folder : context.notes.childFolders(parent)) {
        if (folder == folderPath) {
            return index;
        }
        ++index;
    }
    return -1;
}

bool acceptNoteDropOnFolder(AppContext& context, const std::string& folderPath)
{
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(drag_payload::kNoteId, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
        if (!payload->IsDelivery()) {
            return true;
        }
        std::vector<std::string> ids = activeDragNoteIds();
        if (ids.empty() && payload->Data) {
            ids = {static_cast<const char*>(payload->Data)};
        }
        ids = movableNoteIds(context, ids);
        if (ids.empty()) {
            return true;
        }
        storePendingFromDragPreview(ids);
        std::string error;
        if (context.notes.moveNotes(ids, folderPath, &error)) {
            expandFolderAncestors(folderPath);
            if (!ids.empty()) {
                selectOnlyNote(ids.front());
                context.runtime().selectedNoteId = ids.front();
            }
            clearDragNoteIdsSnapshot();
        } else if (!error.empty()) {
            setStatus(error);
        }
        return true;
    }
    return false;
}

bool acceptNoteReorder(AppContext& context, const std::string& folder, int insertIndexAmongRemaining)
{
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(drag_payload::kNoteId, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
        if (!payload->IsDelivery()) {
            return true;
        }
        std::vector<std::string> ids = activeDragNoteIds();
        if (ids.empty() && payload->Data) {
            ids = {static_cast<const char*>(payload->Data)};
        }
        if (ids.empty()) {
            return true;
        }
        ids = movableNoteIds(context, ids);
        if (ids.empty()) {
            return true;
        }
        storePendingFromDragPreview(ids);
        std::string error;
        if (!context.notes.moveNotes(ids, folder, &error)) {
            if (!error.empty()) {
                setStatus(error);
            }
            return true;
        }
        if (!context.notes.reorderNotesInFolder(ids, insertIndexAmongRemaining, &error)) {
            if (!error.empty()) {
                setStatus(error);
            }
            return true;
        }
        expandFolderAncestors(folder);
        selectOnlyNote(ids.front());
        context.runtime().selectedNoteId = ids.front();
        clearDragNoteIdsSnapshot();
        return true;
    }
    return false;
}

bool acceptFolderReorder(AppContext& context, const std::string& parent, int insertIndex)
{
    if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(drag_payload::kNoteFolder, ImGuiDragDropFlags_AcceptNoDrawDefaultRect)) {
        if (!payload->IsDelivery()) {
            return true;
        }
        const std::string folderPath = static_cast<const char*>(payload->Data);
        if (NotesStore::folderParent(folderPath) != parent) {
            const std::string name = NotesStore::folderName(folderPath);
            const std::string next = parent.empty() ? name : parent + "/" + name;
            if (next == folderPath || NotesStore::isFolderAncestor(folderPath, next)) {
                setStatus(tr("Move failed"));
                return true;
            }
            std::string error;
            if (!context.notes.renameFolder(folderPath, next, &error)) {
                setStatus(error.empty() ? tr("Move failed") : error);
                return true;
            }
            expandFolderAncestors(next);
            context.notes.reorderFolderAmongSiblings(next, insertIndex, &error);
            return true;
        }
        std::string error;
        if (!context.notes.reorderFolderAmongSiblings(folderPath, insertIndex, &error) && !error.empty()) {
            setStatus(error);
        }
        return true;
    }
    return false;
}

void drawFolderContextMenu(AppContext& context, const UiPalette& theme, const std::string& folderPath)
{
    const int popupOpacity = context.themes.active().popupMenuOpacity;
    const UiPalette popupTheme = withPopupOpacity(theme, popupOpacity);
    LightPopupStyle popupStyle(popupTheme, popupOpacity);
    if (!ImGui::BeginPopupContextItem("folder-menu")) {
        return;
    }
    suppressCurrentViewportNativeBorder();
    if (menuItem(popupTheme, Icons::Note, tr("New Note"), "Ctrl+N")) {
        createNoteInFolder(context, folderPath);
    }
    if (menuItem(popupTheme, Icons::Folder, tr("New Subfolder"))) {
        openNewFolderPopup(folderPath);
    }
    if (menuItem(popupTheme, Icons::Edit, tr("Rename"), "F2")) {
        openRenamePopup(true, folderPath, NotesStore::folderName(folderPath));
    }
    if (menuItem(popupTheme, Icons::Paste, tr("Paste"), "Ctrl+V", false, noteClipboardAvailable())) {
        pasteNotesFromClipboard(context, folderPath);
    }
    ImGui::Separator();
    if (menuItem(popupTheme, Icons::Delete, tr("Delete Folder"), "Del")) {
        std::string error;
        if (!context.notes.removeFolder(folderPath, &error)) {
            setStatus(error.empty() ? tr("Delete folder failed") : error);
        } else {
            if (gNotesPanel.selectedFolder == folderPath) {
                gNotesPanel.selectedFolder = NotesStore::folderParent(folderPath);
            }
            setStatus(tr("Folder deleted"));
        }
    }
    ImGui::EndPopup();
}

void drawNoteContextMenu(AppContext& context, const UiPalette& theme, const Note& note)
{
    const int popupOpacity = context.themes.active().popupMenuOpacity;
    const UiPalette popupTheme = withPopupOpacity(theme, popupOpacity);
    LightPopupStyle popupStyle(popupTheme, popupOpacity);
    if (!ImGui::BeginPopupContextItem("note-menu")) {
        return;
    }
    suppressCurrentViewportNativeBorder();
    if (menuItem(popupTheme, Icons::OpenWith, tr("Open"), "Enter")) {
        tryActivateNote(context, note);
        gNotesPanel.mode = 2;
    }
    if (menuItem(popupTheme, Icons::OpenWith, tr("Open File"), "Ctrl+O")) {
        openNoteExternally(context, note);
    }
    if (menuItem(popupTheme, Icons::Edit, tr("Rename"), "F2")) {
        openRenamePopup(false, note.id, NotesStore::displayTitle(note));
    }
    if (beginIconMenu(popupTheme, Icons::Folder, tr("Move to"))) {
        if (menuItem(popupTheme, Icons::Home, tr("Root"))) {
            std::string error;
            if (context.notes.moveNote(note.id, {}, &error)) {
                if (gNotesPanel.loadedNoteId == note.id) {
                    gNotesPanel.selectedFolder.clear();
                }
            } else {
                setStatus(error.empty() ? tr("Move failed") : error);
            }
        }
        for (const std::string& folder : context.notes.folders()) {
            if (folder == note.folder) {
                continue;
            }
            if (menuItem(popupTheme, "", folder.c_str())) {
                std::string error;
                if (context.notes.moveNote(note.id, folder, &error)) {
                    expandFolderAncestors(folder);
                    if (gNotesPanel.loadedNoteId == note.id) {
                        gNotesPanel.selectedFolder = folder;
                    }
                } else {
                    setStatus(error.empty() ? tr("Move failed") : error);
                }
            }
        }
        endIconMenu();
    }
    if (menuItem(popupTheme, note.archived ? Icons::Refresh : Icons::Delete, note.archived ? tr("Restore") : tr("Archive"))) {
        context.notes.archiveNote(note.id, !note.archived);
        context.rebuildSearch();
    }
    ImGui::Separator();
    if (menuItem(popupTheme, Icons::Copy, tr("Copy"), "Ctrl+C")) {
        copyNotesToClipboard(context, selectedOrSingleNoteIds(note.id), false);
    }
    if (menuItem(popupTheme, Icons::Cut, tr("Cut"), "Ctrl+X")) {
        copyNotesToClipboard(context, selectedOrSingleNoteIds(note.id), true);
    }
    if (menuItem(popupTheme, Icons::Paste, tr("Paste"), "Ctrl+V", false, noteClipboardAvailable())) {
        pasteNotesFromClipboard(context, note.folder);
    }
    ImGui::Separator();
    if (menuItem(popupTheme, Icons::Delete, tr("Delete"), "Del")) {
        deleteNotesByIds(context, selectedOrSingleNoteIds(note.id));
        clearNoteSelection();
    }
    ImGui::EndPopup();
}

void drawTreeChildren(AppContext& context, const UiPalette& theme, const NoteTreeNode& parent, float childDepth,
                      const std::unordered_set<std::string>& draggingNoteIds);

void drawTreeNode(AppContext& context, const UiPalette& theme, const NoteTreeNode& node, float depth, int siblingIndex, int siblingCount,
                  const std::unordered_set<std::string>& draggingNoteIds)
{
    if (node.isFolder) {
        ImGui::PushID(node.folderPath.c_str());
        const bool expanded = folderExpanded(node.folderPath);
        const bool selected = gNotesPanel.selectedFolder == node.folderPath && context.runtime().selectedNoteId.empty() &&
                              gNotesPanel.selectedNoteIds.empty();
        const std::string layoutKey = "folder:" + node.folderPath;
        TreeRowResult row =
            drawTreeRow(theme, expanded ? Icons::FolderOpen : Icons::Folder, node.name, selected, false, depth, false, layoutKey.c_str());
        if (row.clicked) {
            // Keep the current note open so expand/collapse does not rebuild the editor pane.
            gNotesPanel.selectedFolder = node.folderPath;
            setFolderExpanded(node.folderPath, !expanded);
        }
        if (row.doubleClicked) {
            setFolderExpanded(node.folderPath, !expanded);
        }
        if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID | ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
            ImGui::SetDragDropPayload(drag_payload::kNoteFolder, node.folderPath.c_str(), static_cast<int>(node.folderPath.size() + 1));
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float mouseY = ImGui::GetIO().MousePos.y;
            const float midY = (row.rect.Min.y + row.rect.Max.y) * 0.5f;
            const bool insertBefore = mouseY < midY;
            const ImGuiPayload* active = ImGui::GetDragDropPayload();
            if (active && active->IsDataType(drag_payload::kNoteId)) {
                dl->AddRect(row.rect.Min, row.rect.Max, ImGui::ColorConvertFloat4ToU32(theme.frameActive), theme.itemRounding, 0, 1.5f);
                acceptNoteDropOnFolder(context, node.folderPath);
            } else if (active && active->IsDataType(drag_payload::kNoteFolder)) {
                const float lineY = insertBefore ? row.rect.Min.y : row.rect.Max.y;
                dl->AddLine(ImVec2(row.rect.Min.x + 4.0f, lineY), ImVec2(row.rect.Max.x - 4.0f, lineY),
                            ImGui::ColorConvertFloat4ToU32(theme.frameActive), 2.0f);
                acceptFolderReorder(context, NotesStore::folderParent(node.folderPath), insertBefore ? siblingIndex : siblingIndex + 1);
            }
            ImGui::EndDragDropTarget();
        }
        drawFolderContextMenu(context, theme, node.folderPath);
        ImGui::PopID();
        if (expanded) {
            drawTreeChildren(context, theme, node, depth + 1.0f, draggingNoteIds);
        }
        return;
    }

    const Note* note = context.notes.find(node.noteId);
    if (!note) {
        return;
    }
    ImGui::PushID(node.noteId.c_str());
    const bool multiSelected = isNoteSelected(note->id);
    const bool selected = multiSelected || context.runtime().selectedNoteId == note->id;
    std::string label;
    if (note->pinned) {
        label += std::string(Icons::TopMost) + " ";
    }
    if (note->fixed) {
        label += std::string(Icons::Pin) + " ";
    }
    label += node.name;
    if (gNotesPanel.dirty && gNotesPanel.loadedNoteId == note->id) {
        label += " *";
    }
    if (note->archived) {
        label += " [A]";
    }
    const bool isDraggingThis = draggingNoteIds.contains(note->id);
    TreeRowResult row = drawTreeRow(theme, Icons::Note, label, selected, note->archived, depth, false, note->id.c_str(), isDraggingThis);
    if (!isDraggingThis) {
        const ImGuiIO& io = ImGui::GetIO();
        // On press: remember multi state so we can collapse only if this press does not become a drag.
        if (row.clicked) {
            gNotesPanel.pressNoteId = note->id;
            gNotesPanel.pressWasMulti = isNoteSelected(note->id) && gNotesPanel.selectedNoteIds.size() > 1 && !io.KeyCtrl && !io.KeyShift;
            gNotesPanel.pressHandledAsDrag = false;
            if (io.KeyCtrl) {
                toggleNoteSelection(note->id);
                context.runtime().selectedNoteId = note->id;
                gNotesPanel.selectedFolder = note->folder;
                if (isNoteSelected(note->id) && !gNotesPanel.dirty) {
                    loadDraft(*note);
                }
                gNotesPanel.pressNoteId.clear();
                gNotesPanel.pressWasMulti = false;
            } else if (io.KeyShift) {
                if (!gNotesPanel.dirty) {
                    selectNoteRangeCached(note->id, context.runtime().selectedNoteId);
                    if (gNotesPanel.selectedNoteIds.empty()) {
                        selectOnlyNote(note->id);
                    }
                    context.runtime().selectedNoteId = note->id;
                    gNotesPanel.selectedFolder = note->folder;
                    loadDraft(*note);
                } else {
                    tryActivateNote(context, *note);
                }
                gNotesPanel.pressNoteId.clear();
                gNotesPanel.pressWasMulti = false;
            } else if (!gNotesPanel.pressWasMulti) {
                // Immediate single select when not collapsing a multi-selection.
                tryActivateNote(context, *note);
                gNotesPanel.pressNoteId.clear();
            } else {
                // Keep multi until release/drag decision; do not auto-load another draft while dirty.
                if (!gNotesPanel.dirty) {
                    context.runtime().selectedNoteId = note->id;
                    gNotesPanel.selectedFolder = note->folder;
                }
            }
        }

        // Fixed notes are locked in place; pinned-only notes can still sort inside the pinned group and drag to the list.
        if (!note->fixed && ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID | ImGuiDragDropFlags_SourceNoPreviewTooltip)) {
            // Capture current multi-selection before any collapse; drop fixed ids from the payload set.
            captureDragNoteIds(note->id);
            std::vector<std::string> filtered = movableNoteIds(context, gNotesPanel.dragNoteIdsSnapshot);
            if (filtered.empty()) {
                filtered.push_back(note->id);
            }
            gNotesPanel.dragNoteIdsSnapshot = std::move(filtered);
            gNotesPanel.dragPrimaryNoteId = note->id;
            ImGui::SetDragDropPayload(drag_payload::kNoteId, note->id.c_str(), static_cast<int>(note->id.size() + 1));
            gNotesPanel.pressHandledAsDrag = true;
            gNotesPanel.pressNoteId.clear();
            gNotesPanel.pressWasMulti = false;
            ImGui::EndDragDropSource();
        }

        // Collapse multi-selection on plain click release (no drag).
        if (!gNotesPanel.pressNoteId.empty() && gNotesPanel.pressNoteId == note->id && gNotesPanel.pressWasMulti &&
            !gNotesPanel.pressHandledAsDrag && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
            !(ImGui::GetDragDropPayload() && ImGui::GetDragDropPayload()->IsDataType(drag_payload::kNoteId))) {
            tryActivateNote(context, *note);
            gNotesPanel.pressNoteId.clear();
            gNotesPanel.pressWasMulti = false;
        }
        if (row.doubleClicked) {
            tryActivateNote(context, *note, true);
        }
        drawNoteContextMenu(context, theme, *note);
    }
    ImGui::PopID();
}

void drawTreeChildren(AppContext& context, const UiPalette& theme, const NoteTreeNode& parent, float childDepth,
                      const std::unordered_set<std::string>& draggingNoteIds)
{
    std::vector<const NoteTreeNode*> folders;
    std::vector<const NoteTreeNode*> pinnedNotes;
    std::vector<const NoteTreeNode*> plainNotes;
    for (const NoteTreeNode& child : parent.children) {
        if (child.isFolder) {
            folders.push_back(&child);
        } else if (child.pinned) {
            pinnedNotes.push_back(&child);
        } else {
            plainNotes.push_back(&child);
        }
    }

    int folderIndex = 0;
    for (const NoteTreeNode* child : folders) {
        drawTreeNode(context, theme, *child, childDepth, folderIndex, static_cast<int>(folders.size()), draggingNoteIds);
        ++folderIndex;
    }

    auto drawNoteGroup = [&](const std::vector<const NoteTreeNode*>& group, const char* groupId, bool groupPinned) {
        std::vector<const NoteTreeNode*> remaining;
        remaining.reserve(group.size());
        for (const NoteTreeNode* child : group) {
            if (!draggingNoteIds.contains(child->noteId)) {
                remaining.push_back(child);
            }
        }

        int insertAmongRemaining = -1;
        if (!draggingNoteIds.empty()) {
            bool allMatch = true;
            for (const std::string& id : draggingNoteIds) {
                const Note* n = context.notes.find(id);
                if (!n || n->fixed || n->pinned != groupPinned) {
                    allMatch = false;
                    break;
                }
            }
            if (allMatch) {
                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const ImVec2 blockStart = ImGui::GetCursorScreenPos();
                const float blockTop = blockStart.y;
                const float blockBottom = blockTop + kTreeRowHeight * static_cast<float>(remaining.size());
                const float topPad = 6.0f;
                const float bottomPad = kTreeRowHeight * 0.6f;
                if (mouse.y >= blockTop - topPad && mouse.y <= blockBottom + bottomPad) {
                    if (remaining.empty()) {
                        insertAmongRemaining = 0;
                    } else {
                        const float rel = mouse.y - blockTop;
                        const int index = static_cast<int>(std::floor(rel / kTreeRowHeight + 0.5f));
                        insertAmongRemaining = std::clamp(index, 0, static_cast<int>(remaining.size()));
                    }
                }
            }
        }

        const int ghostCount = insertAmongRemaining >= 0 ? std::max(1, std::min(3, static_cast<int>(draggingNoteIds.size()))) : 0;
        auto drawGhosts = [&]() {
            if (ghostCount <= 0) {
                return;
            }
            const float width = ImGui::GetContentRegionAvail().x;
            for (int g = 0; g < ghostCount; ++g) {
                const ImVec2 pos = ImGui::GetCursorScreenPos();
                drawNoteTreeGhostSlot(theme, pos, width);
                ImGui::Dummy(ImVec2(width, kTreeRowHeight));
            }
        };

        const ImVec2 dropMin = ImGui::GetCursorScreenPos();
        const float dropWidth = ImGui::GetContentRegionAvail().x;
        const int visualRows = static_cast<int>(remaining.size()) + (insertAmongRemaining >= 0 ? ghostCount : 0);
        const float dropHeight = std::max(kTreeRowHeight, kTreeRowHeight * static_cast<float>(std::max(1, visualRows)));

        if (insertAmongRemaining == 0) {
            drawGhosts();
        }
        int remainingIndex = 0;
        for (const NoteTreeNode* child : remaining) {
            drawTreeNode(context, theme, *child, childDepth, remainingIndex, static_cast<int>(remaining.size()), draggingNoteIds);
            ++remainingIndex;
            if (insertAmongRemaining == remainingIndex) {
                drawGhosts();
            }
        }

        if (!draggingNoteIds.empty() && insertAmongRemaining >= 0) {
            // Convert group-local remaining index into full-folder remaining index
            // (non-dragged notes, display order: pinned then plain).
            int fullInsert = 0;
            if (!groupPinned) {
                for (const NoteTreeNode* n : pinnedNotes) {
                    if (!draggingNoteIds.contains(n->noteId)) {
                        ++fullInsert;
                    }
                }
            }
            fullInsert += insertAmongRemaining;

            ImGui::PushID((parent.folderPath + "##" + groupId).c_str());
            const ImRect dropRect(dropMin, ImVec2(dropMin.x + dropWidth, dropMin.y + dropHeight + 8.0f));
            if (ImGui::BeginDragDropTargetCustom(dropRect, ImGui::GetID("note-group-reorder-zone"))) {
                acceptNoteReorder(context, parent.folderPath, fullInsert);
                ImGui::EndDragDropTarget();
            }
            ImGui::PopID();
        }
    };

    drawNoteGroup(pinnedNotes, "pinned", true);
    drawNoteGroup(plainNotes, "plain", false);
}

void drawNotesDirectory(AppContext& context, const UiPalette& theme)
{
    ImGui::PushStyleColor(ImGuiCol_FrameBg, colorWithAlpha(theme.frameBg, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme.frameHovered);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, theme.frameActive);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.frameRounding);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##notes-filter", tr("Search Notes"), &gNotesPanel.filter);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(3);

    ImGui::Spacing();
    ui_anim::checkbox(tr("Show archived"), &gNotesPanel.includeArchived);
    ImGui::Dummy(ImVec2(1.0f, 4.0f));

    const NoteTreeNode tree = buildNoteTree(context);
    gNotesPanel.lastVisibleNoteIds = visibleNoteIdsInTree(tree);
    if (!gNotesPanel.pendingRangeToNoteId.empty()) {
        selectNoteRangeFromVisible(gNotesPanel.lastVisibleNoteIds, gNotesPanel.pendingRangeToNoteId, context.runtime().selectedNoteId);
        gNotesPanel.pendingRangeToNoteId.clear();
    }
    const ImGuiPayload* payload = ImGui::GetDragDropPayload();
    const bool dragLive = payload && payload->IsDataType(drag_payload::kNoteId);
    const std::vector<std::string> dragging = activeDragNoteIds();
    const std::unordered_set<std::string> draggingSet =
        dragLive ? std::unordered_set<std::string>(dragging.begin(), dragging.end()) : std::unordered_set<std::string>{};
    if (dragLive && !dragging.empty()) {
        drawFloatingNoteDragPreview(theme, dragging, context);
    } else {
        gNotesPanel.dragPreviewAnchor.reset();
        if (!dragLive) {
            // Keep snapshot only through delivery; clear once drag fully ends.
            // Delivery handlers clear themselves; this covers cancelled drags.
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                clearDragNoteIdsSnapshot();
            }
        }
    }

    ImGui::BeginChild("notes-tree-items", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_None);
    if (tree.children.empty()) {
        ImGui::TextDisabled("%s", tr("No notes"));
        ImGui::InvisibleButton("notes-tree-empty-drop", ImVec2(-FLT_MIN, 48.0f));
        if (ImGui::BeginDragDropTarget()) {
            acceptNoteDropOnFolder(context, {});
            ImGui::EndDragDropTarget();
        }
    } else {
        drawTreeChildren(context, theme, tree, 0.0f, draggingSet);
        int folderCount = 0;
        int noteCount = 0;
        for (const NoteTreeNode& child : tree.children) {
            if (child.isFolder) {
                ++folderCount;
            } else if (!draggingSet.contains(child.noteId)) {
                ++noteCount;
            }
        }
        ImGui::InvisibleButton("notes-tree-tail-drop", ImVec2(-FLT_MIN, 12.0f));
        if (ImGui::BeginDragDropTarget()) {
            acceptNoteReorder(context, {}, noteCount);
            acceptFolderReorder(context, {}, folderCount);
            ImGui::EndDragDropTarget();
        }
    }
    ImGui::EndChild();
}

void applyEditorOutlineScroll()
{
    if (gNotesPanel.outline.pendingLine < 0 || gNotesPanel.outline.editorScrollFrames <= 0) {
        return;
    }
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (!window) {
        return;
    }
    const float lineHeight = ImGui::GetTextLineHeight();
    const float viewHeight = std::max(1.0f, window->InnerRect.GetHeight());
    const float targetY = static_cast<float>(gNotesPanel.outline.pendingLine) * lineHeight;
    const float desired = targetY - viewHeight * 0.20f;
    const float maxScroll = std::max(0.0f, window->ScrollMax.y);
    ImGui::SetScrollY(std::clamp(desired, 0.0f, maxScroll));
    --gNotesPanel.outline.editorScrollFrames;
    if (gNotesPanel.outline.editorScrollFrames <= 0) {
        gNotesPanel.outline.pendingLine = -1;
    }
}

void drawNoteMetadataInputs(const UiPalette& theme)
{
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, theme.frameRounding);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, colorWithAlpha(theme.frameBg, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, theme.frameHovered);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, theme.frameActive);
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##note-title", tr("Title"), &gNotesPanel.draftTitle)) {
        markDirty();
    }
    ImGui::SetNextItemWidth(-FLT_MIN);
    if (ImGui::InputTextWithHint("##note-tags", tr("Tags"), &gNotesPanel.draftTags)) {
        markDirty();
    }
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
}

float markdownTextContentWidth(const std::string& text)
{
    float width = 0.0f;
    size_t lineStart = 0;
    bool done = false;
    while (!done && lineStart <= text.size()) {
        const size_t lineEnd = text.find('\n', lineStart);
        const std::string line = text.substr(lineStart, lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart);
        width = std::max(width, ImGui::CalcTextSize(line.c_str()).x);
        if (lineEnd == std::string::npos) {
            done = true;
        } else {
            lineStart = lineEnd + 1;
        }
    }
    return width + ImGui::GetStyle().FramePadding.x * 2.0f + 24.0f;
}

int countLines(const std::string& text)
{
    if (text.empty()) {
        return 1;
    }
    int lines = 1;
    for (char ch : text) {
        if (ch == '\n') {
            ++lines;
        }
    }
    return lines;
}

struct OutlineScrollCallbackData {
    int targetLine = -1;
    bool applied = false;
};

int outlineScrollCallback(ImGuiInputTextCallbackData* data)
{
    auto* state = static_cast<OutlineScrollCallbackData*>(data->UserData);
    if (!state || state->applied || state->targetLine < 0 || data->EventFlag != ImGuiInputTextFlags_CallbackAlways) {
        return 0;
    }
    int line = 0;
    int pos = 0;
    for (int i = 0; i < data->BufTextLen; ++i) {
        if (line == state->targetLine) {
            pos = i;
            break;
        }
        if (data->Buf[i] == '\n') {
            ++line;
            pos = i + 1;
        }
        if (i + 1 == data->BufTextLen) {
            pos = data->BufTextLen;
        }
    }
    data->CursorPos = std::clamp(pos, 0, data->BufTextLen);
    data->SelectionStart = data->CursorPos;
    data->SelectionEnd = data->CursorPos;
    state->applied = true;
    return 0;
}

void captureTextSelectionFromLastItem()
{
    gNotesPanel.textFieldId = ImGui::GetItemID();
    if (ImGuiInputTextState* state = ImGui::GetInputTextState(gNotesPanel.textFieldId)) {
        if (state->HasSelection()) {
            gNotesPanel.textSelStart = state->GetSelectionStart();
            gNotesPanel.textSelEnd = state->GetSelectionEnd();
        } else {
            const int cursor = state->GetCursorPos();
            gNotesPanel.textSelStart = cursor;
            gNotesPanel.textSelEnd = cursor;
        }
    }
}

void normalizeTextSelection(int& start, int& end, int textLen)
{
    start = std::clamp(start, 0, textLen);
    end = std::clamp(end, 0, textLen);
    if (start > end) {
        std::swap(start, end);
    }
}

std::string selectedDraftText()
{
    int start = gNotesPanel.textSelStart;
    int end = gNotesPanel.textSelEnd;
    normalizeTextSelection(start, end, static_cast<int>(gNotesPanel.draftBody.size()));
    return gNotesPanel.draftBody.substr(static_cast<size_t>(start), static_cast<size_t>(end - start));
}

void applyDraftTextMutation(int start, int end, const std::string& insert)
{
    normalizeTextSelection(start, end, static_cast<int>(gNotesPanel.draftBody.size()));
    gNotesPanel.draftBody.replace(static_cast<size_t>(start), static_cast<size_t>(end - start), insert);
    markDirty();
    if (ImGuiInputTextState* state = ImGui::GetInputTextState(gNotesPanel.textFieldId)) {
        state->ReloadUserBufAndKeepSelection();
        const int cursor = start + static_cast<int>(insert.size());
        state->SetSelection(cursor, cursor);
    }
    gNotesPanel.textSelStart = start + static_cast<int>(insert.size());
    gNotesPanel.textSelEnd = gNotesPanel.textSelStart;
}

void restoreTextSelectionHighlight();

void restoreTextSelectionHighlight()
{
    if (gNotesPanel.textFieldId == 0) {
        return;
    }
    if (ImGuiInputTextState* state = ImGui::GetInputTextState(gNotesPanel.textFieldId)) {
        int start = gNotesPanel.textSelStart;
        int end = gNotesPanel.textSelEnd;
        normalizeTextSelection(start, end, state->TextLen);
        if (start != end) {
            state->SetSelection(start, end);
        }
    }
}

void drawTextEditContextMenu(const UiPalette& theme, bool readOnly = false)
{
    captureTextSelectionFromLastItem();
    const int popupOpacity = 100;
    const UiPalette popupTheme = withPopupOpacity(theme, popupOpacity);
    LightPopupStyle popupStyle(popupTheme, popupOpacity);
    if (!ImGui::BeginPopupContextItem("note-text-menu", ImGuiPopupFlags_MouseButtonRight)) {
        if (gNotesPanel.textMenuOpen) {
            gNotesPanel.textMenuOpen = false;
            restoreTextSelectionHighlight();
        }
        return;
    }
    gNotesPanel.textMenuOpen = true;
    suppressCurrentViewportNativeBorder();
    restoreTextSelectionHighlight();
    int start = gNotesPanel.textSelStart;
    int end = gNotesPanel.textSelEnd;
    normalizeTextSelection(start, end, static_cast<int>(gNotesPanel.draftBody.size()));
    const bool hasSelection = start != end;
    const char* clipboard = ImGui::GetClipboardText();
    const bool canPaste = clipboard && clipboard[0] != '\0';

    if (!readOnly) {
        if (menuItem(popupTheme, Icons::Cut, tr("Cut"), "Ctrl+X", false, hasSelection)) {
            ImGui::SetClipboardText(selectedDraftText().c_str());
            applyDraftTextMutation(start, end, {});
        }
    }
    if (menuItem(popupTheme, Icons::Copy, tr("Copy"), "Ctrl+C", false, hasSelection || readOnly)) {
        if (hasSelection) {
            ImGui::SetClipboardText(selectedDraftText().c_str());
        } else {
            ImGui::SetClipboardText(gNotesPanel.draftBody.c_str());
        }
    }
    if (!readOnly) {
        if (menuItem(popupTheme, Icons::Paste, tr("Paste"), "Ctrl+V", false, canPaste)) {
            applyDraftTextMutation(start, end, clipboard ? clipboard : "");
        }
        if (menuItem(popupTheme, Icons::Delete, tr("Delete"), "Del", false, hasSelection)) {
            applyDraftTextMutation(start, end, {});
        }
        ImGui::Separator();
        if (menuItem(popupTheme, Icons::Check, tr("Select All"), "Ctrl+A")) {
            gNotesPanel.textSelStart = 0;
            gNotesPanel.textSelEnd = static_cast<int>(gNotesPanel.draftBody.size());
            if (ImGuiInputTextState* state = ImGui::GetInputTextState(gNotesPanel.textFieldId)) {
                state->SelectAll();
            }
        }
    } else {
        ImGui::Separator();
        if (menuItem(popupTheme, Icons::Copy, tr("Copy All"))) {
            ImGui::SetClipboardText(gNotesPanel.draftBody.c_str());
        }
    }
    ImGui::EndPopup();
}

void drawEditableMarkdown(const UiPalette& theme, const char* id, const ImVec2& size)
{
    pushPaneStyle(theme);
    ImGui::BeginChild(id, size, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
    const float lineHeight = ImGui::GetTextLineHeight();
    const float editorWidth = std::max(ImGui::GetContentRegionAvail().x, markdownTextContentWidth(gNotesPanel.draftBody));
    const float contentHeight =
        std::max(ImGui::GetContentRegionAvail().y, static_cast<float>(countLines(gNotesPanel.draftBody)) * lineHeight + 32.0f);
    OutlineScrollCallbackData callbackData;
    callbackData.targetLine = gNotesPanel.outline.pendingLine;
    ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
    if (callbackData.targetLine >= 0) {
        flags |= ImGuiInputTextFlags_CallbackAlways;
    }
    if (ImGui::InputTextMultiline("##note-body", &gNotesPanel.draftBody, ImVec2(editorWidth, contentHeight), flags,
                                  callbackData.targetLine >= 0 ? outlineScrollCallback : nullptr, &callbackData)) {
        markDirty();
    }
    captureTextSelectionFromLastItem();
    if (gNotesPanel.textMenuOpen) {
        restoreTextSelectionHighlight();
    }
    drawTextEditContextMenu(theme, false);
    if (gNotesPanel.outline.pendingLine >= 0) {
        applyEditorOutlineScroll();
    }
    ImGui::PopStyleColor();
    ImGui::EndChild();
    popPaneStyle();
}

void drawRenderedMarkdown(const UiPalette& theme, const char* id, const ImVec2& size)
{
    pushPaneStyle(theme);
    ImGui::BeginChild(id, size, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    drawMarkdownPreview(theme, gNotesPanel.draftBody, &gNotesPanel.outline);
    ImGui::EndChild();
    popPaneStyle();
}

void drawSelectablePreviewPane(const UiPalette& theme, const ImVec2& size)
{
    pushPaneStyle(theme);
    ImGui::BeginChild("note-selectable-preview-pane", size, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    launcher::drawSelectableMarkdownPreview(theme, gNotesPanel.draftBody, &gNotesPanel.outline);
    ImGui::EndChild();
    popPaneStyle();
}

void drawEditor(AppContext& context, const UiPalette& theme, Note& note)
{
    (void)note;
    (void)context;
    if (notesButton(theme, Icons::CopyProperties, tr("Preview"), gNotesPanel.mode == 2)) {
        saveDraft(context, true);
        gNotesPanel.mode = 2;
    }
    ImGui::SameLine();
    if (notesButton(theme, Icons::Edit, tr("Edit"), gNotesPanel.mode == 0)) {
        gNotesPanel.mode = 0;
    }
    ImGui::SameLine();
    if (notesButton(theme, Icons::Layout, tr("Split"), gNotesPanel.mode == 1)) {
        gNotesPanel.mode = 1;
    }
    ImGui::Dummy(ImVec2(1.0f, 8.0f));

    if (gNotesPanel.mode == 2) {
        const std::string title = gNotesPanel.draftTitle.empty() ? tr("Untitled Note") : gNotesPanel.draftTitle;
        ImGui::TextUnformatted(title.c_str());
        if (!gNotesPanel.draftTags.empty()) {
            ImGui::TextDisabled("%s", gNotesPanel.draftTags.c_str());
        }
        ImGui::Dummy(ImVec2(1.0f, 8.0f));
    } else {
        drawNoteMetadataInputs(theme);
        ImGui::Dummy(ImVec2(1.0f, 8.0f));
    }

    const float editorHeight = std::max(120.0f, ImGui::GetContentRegionAvail().y);
    if (gNotesPanel.mode == 0) {
        drawEditableMarkdown(theme, "note-edit-pane", ImVec2(-FLT_MIN, editorHeight));
    } else if (gNotesPanel.mode == 2) {
        drawSelectablePreviewPane(theme, ImVec2(-FLT_MIN, editorHeight));
    } else {
        // Split: shared scroll progress; scrollbar lives on the right (preview) only.
        const float width = ImGui::GetContentRegionAvail().x;
        const float leftWidth = std::max(160.0f, (width - kPaneGap) * 0.5f);
        const float rightWidth = std::max(120.0f, width - leftWidth - kPaneGap);

        // Shared vertical progress; only the right (preview) shows a vertical scrollbar.
        // Both panes accept wheel scroll and stay synchronized by ratio.
        ImGui::BeginChild("note-split-editor-pane", ImVec2(leftWidth, editorHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        const float lineHeight = ImGui::GetTextLineHeight();
        const float editorWidth = std::max(ImGui::GetContentRegionAvail().x, markdownTextContentWidth(gNotesPanel.draftBody));
        const float contentHeight =
            std::max(ImGui::GetContentRegionAvail().y, static_cast<float>(countLines(gNotesPanel.draftBody)) * lineHeight + 32.0f);
        OutlineScrollCallbackData callbackData;
        callbackData.targetLine = gNotesPanel.outline.pendingLine;
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput;
        if (callbackData.targetLine >= 0) {
            flags |= ImGuiInputTextFlags_CallbackAlways;
        }
        if (ImGui::InputTextMultiline("##note-body", &gNotesPanel.draftBody, ImVec2(editorWidth, contentHeight), flags,
                                      callbackData.targetLine >= 0 ? outlineScrollCallback : nullptr, &callbackData)) {
            markDirty();
        }
        captureTextSelectionFromLastItem();
        if (gNotesPanel.textMenuOpen) {
            restoreTextSelectionHighlight();
        }
        drawTextEditContextMenu(theme, false);
        if (gNotesPanel.outline.pendingLine >= 0) {
            applyEditorOutlineScroll();
        }
        ImGui::PopStyleColor();
        ImGuiWindow* editorWindow = ImGui::GetCurrentWindow();
        const bool editorHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (editorWindow) {
            editorWindow->ScrollbarY = false;
            editorWindow->ScrollbarSizes.x = 0.0f;
            const float maxY = std::max(0.0f, editorWindow->ScrollMax.y);
            if (editorHovered && maxY > 0.0f) {
                // Wheel / drag on editor updates shared ratio.
                gNotesPanel.splitScrollRatio = std::clamp(editorWindow->Scroll.y / maxY, 0.0f, 1.0f);
            } else if (maxY > 0.0f) {
                editorWindow->Scroll.y = maxY * gNotesPanel.splitScrollRatio;
            } else {
                editorWindow->Scroll.y = 0.0f;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine(0.0f, (kPaneGap - 1.0f) * 0.5f);
        drawVerticalPaneSeparator(theme, editorHeight);
        ImGui::SameLine(0.0f, (kPaneGap - 1.0f) * 0.5f);

        // Right pane owns the only vertical scrollbar; horizontal remains independent.
        ImGui::BeginChild("note-split-preview-pane", ImVec2(rightWidth, editorHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        drawMarkdownPreview(theme, gNotesPanel.draftBody, &gNotesPanel.outline);
        ImGuiWindow* previewWindow = ImGui::GetCurrentWindow();
        const bool previewHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (previewWindow) {
            const float maxY = std::max(0.0f, previewWindow->ScrollMax.y);
            if (previewHovered && !editorHovered && maxY > 0.0f) {
                gNotesPanel.splitScrollRatio = std::clamp(previewWindow->Scroll.y / maxY, 0.0f, 1.0f);
            } else if (maxY > 0.0f) {
                previewWindow->Scroll.y = maxY * gNotesPanel.splitScrollRatio;
            } else {
                previewWindow->Scroll.y = 0.0f;
            }
        }
        ImGui::EndChild();

        // Keep editor matched after preview scrollbar drag in the same frame.
        if (editorWindow) {
            const float maxY = std::max(0.0f, editorWindow->ScrollMax.y);
            if (maxY > 0.0f) {
                editorWindow->Scroll.y = maxY * gNotesPanel.splitScrollRatio;
            }
        }
    }
}

void drawQuickEditor(AppContext& context, const UiPalette& theme, Note& note)
{
    if (notesButton(theme, Icons::CopyProperties, tr("Preview"), gNotesPanel.mode == 2)) {
        saveDraft(context, true);
        gNotesPanel.mode = 2;
    }
    ImGui::SameLine();
    if (notesButton(theme, Icons::Edit, tr("Edit"), gNotesPanel.mode == 0)) {
        gNotesPanel.mode = 0;
    }
    ImGui::SameLine();
    if (notesButton(theme, Icons::Layout, tr("Outline"), gNotesPanel.outline.showOutline)) {
        gNotesPanel.outline.showOutline = !gNotesPanel.outline.showOutline;
    }
    ImGui::Dummy(ImVec2(1.0f, 8.0f));

    const std::string title = gNotesPanel.draftTitle.empty() ? NotesStore::displayTitle(note) : gNotesPanel.draftTitle;
    ImGui::TextUnformatted(title.c_str());
    if (!gNotesPanel.draftTags.empty()) {
        ImGui::TextDisabled("%s", gNotesPanel.draftTags.c_str());
    }
    ImGui::Dummy(ImVec2(1.0f, 8.0f));

    const float bodyHeight = std::max(140.0f, ImGui::GetContentRegionAvail().y);
    const float bodyWidth = ImGui::GetContentRegionAvail().x;
    const float outlineWidth = gNotesPanel.outline.showOutline ? std::clamp(bodyWidth * 0.26f, kOutlineWidthMin, kOutlineWidthMax) : 0.0f;
    const float contentWidth = std::max(160.0f, bodyWidth - outlineWidth - (gNotesPanel.outline.showOutline ? kPaneGap : 0.0f));

    if (gNotesPanel.mode == 0) {
        drawEditableMarkdown(theme, "quick-note-edit-pane", ImVec2(contentWidth, bodyHeight));
    } else {
        pushPaneStyle(theme);
        ImGui::BeginChild("quick-note-preview-pane", ImVec2(contentWidth, bodyHeight), ImGuiChildFlags_None,
                          ImGuiWindowFlags_HorizontalScrollbar);
        launcher::drawSelectableMarkdownPreview(theme, gNotesPanel.draftBody, &gNotesPanel.outline);
        ImGui::EndChild();
        popPaneStyle();
    }

    if (gNotesPanel.outline.showOutline) {
        ImGui::SameLine(0.0f, (kPaneGap - 1.0f) * 0.5f);
        drawVerticalPaneSeparator(theme, bodyHeight);
        ImGui::SameLine(0.0f, (kPaneGap - 1.0f) * 0.5f);
        pushPaneStyle(theme);
        ImGui::BeginChild("quick-note-outline", ImVec2(outlineWidth, bodyHeight), ImGuiChildFlags_None);
        drawMarkdownOutlinePanel(theme, gNotesPanel.draftBody, gNotesPanel.outline);
        ImGui::EndChild();
        popPaneStyle();
    }
}

void drawQuickNotePanel(AppContext& context, const UiPalette& theme)
{
    if (!context.runtime().showNoteQuick) {
        return;
    }

    ensureSelection(context);
    Note* selected = context.notes.find(context.runtime().selectedNoteId);
    if (selected && gNotesPanel.loadedNoteId != selected->id) {
        loadDraft(*selected);
    }
    if (!selected) {
        context.runtime().showNoteQuick = false;
        return;
    }

    setupManagedWindow("LauncherManagedNoteQuick");
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(900.0f, 620.0f), ImGuiCond_FirstUseEver);
    bool open = context.runtime().showNoteQuick;
    ManagedWindowStyle windowStyle(theme);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));
    if (!ImGui::Begin("Note###note-quick", &open, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
        ImGui::PopStyleVar();
        context.runtime().showNoteQuick = open;
        ImGui::End();
        return;
    }
    if (!open) {
        open = true;
        context.runtime().showNoteQuick = true;
        requestCloseNotes(context, true);
    } else {
        context.runtime().showNoteQuick = true;
    }
    applyManagedViewportChrome(ImGui::GetWindowViewport()->PlatformHandleRaw, context.themes.active(), theme);
    std::string quickTitle = NotesStore::displayTitle(*selected);
    if (gNotesPanel.dirty) {
        quickTitle += " *";
    }
    bool titleOpen = true;
    drawManagedTitleBar(theme, quickTitle.c_str(), titleOpen);
    if (!titleOpen) {
        requestCloseNotes(context, true);
    }
    if (!context.runtime().showNoteQuick) {
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }

    ui_anim::pushAppearAlpha(ImGui::GetID("note-quick-appear"), 0.14f, 0.2f);
    ImGui::SetCursorPos(ImVec2(kOuterPadding, kUiTitleHeight + kOuterPadding));
    ImGui::BeginChild("note-quick-content", ImVec2(-kOuterPadding, -kOuterPadding), ImGuiChildFlags_None);
    drawQuickEditor(context, theme, *selected);
    ImGui::EndChild();
    drawCloseConfirmPopup(context, theme);
    ui_anim::popAppearAlpha();

    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace

bool addNoteIdsAsListItems(AppContext& context, const std::vector<std::string>& noteIds)
{
    return addNoteIdsAsListItemsImpl(context, noteIds);
}

std::vector<std::string> activeDragNoteIdsForDrop()
{
    return activeDragNoteIds();
}

void drawNotesPanel(AppContext& context, const UiPalette& theme)
{
    if (!context.runtime().showNotes && !context.runtime().showNoteQuick) {
        // Manual save only: never force-save on hide. Confirm dialogs already resolved dirty state.
        if (gNotesPanelWasVisible) {
            clearNotesPanelState();
            gNotesTrimFramesRemaining = 4;
            gNotesPanelWasVisible = false;
        }
        if (gNotesTrimFramesRemaining > 0) {
            trimNotesWorkingSet();
            --gNotesTrimFramesRemaining;
        }
        return;
    }
    gNotesPanelWasVisible = true;
    gNotesTrimFramesRemaining = 0;

    drawQuickNotePanel(context, theme);
    if (!context.runtime().showNotes) {
        return;
    }

    ensureSelection(context);
    Note* selected = context.notes.find(context.runtime().selectedNoteId);
    if (selected && gNotesPanel.loadedNoteId != selected->id) {
        loadDraft(*selected);
    }
    if (selected && context.runtime().editSelectedNote) {
        gNotesPanel.mode = 0;
        context.runtime().editSelectedNote = false;
    }

    setupManagedWindow("LauncherManagedNotes");
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(1100.0f, 680.0f), ImGuiCond_FirstUseEver);
    bool open = context.runtime().showNotes;
    ManagedWindowStyle windowStyle(theme);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(10.0f, 8.0f));
    if (!ImGui::Begin("Notes###notes-panel", &open,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
        ImGui::PopStyleVar();
        context.runtime().showNotes = open;
        ImGui::End();
        return;
    }
    if (!open) {
        // Title close requested; keep window open until user resolves unsaved changes.
        open = true;
        context.runtime().showNotes = true;
        requestCloseNotes(context, false);
    } else {
        context.runtime().showNotes = true;
    }
    applyManagedViewportChrome(ImGui::GetWindowViewport()->PlatformHandleRaw, context.themes.active(), theme);
    const std::string title = gNotesPanel.dirty ? (std::string(tr("Notes")) + " *") : std::string(tr("Notes"));
    bool titleOpen = true;
    drawManagedTitleBar(theme, title.c_str(), titleOpen);
    if (!titleOpen) {
        requestCloseNotes(context, false);
    }
    if (!context.runtime().showNotes) {
        ImGui::PopStyleVar();
        ImGui::End();
        return;
    }
    ui_anim::pushAppearAlpha(ImGui::GetID("notes-window-appear"), 0.14f, 0.2f);

    ImGui::SetCursorPos(ImVec2(kOuterPadding, kUiTitleHeight + kOuterPadding));
    ImGui::BeginChild("notes-toolbar", ImVec2(-kOuterPadding, kToolbarHeight), ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    drawNotesToolbar(context, theme, selected);
    ImGui::EndChild();

    // Notes shortcuts (when not typing in text fields, still allow Ctrl+S while editing).
    {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
            markDirty();
            saveDraft(context, true);
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false) && !io.WantTextInput) {
            createNoteInFolder(context, gNotesPanel.selectedFolder);
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && !io.WantTextInput) {
            const std::string id = context.runtime().selectedNoteId;
            if (!id.empty()) {
                copyNotesToClipboard(context, selectedOrSingleNoteIds(id), false);
            }
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false) && !io.WantTextInput) {
            const std::string id = context.runtime().selectedNoteId;
            if (!id.empty()) {
                copyNotesToClipboard(context, selectedOrSingleNoteIds(id), true);
            }
        } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false) && !io.WantTextInput) {
            pasteNotesFromClipboard(context, gNotesPanel.selectedFolder);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) && !io.WantTextInput) {
            const std::string id = context.runtime().selectedNoteId;
            if (!id.empty()) {
                deleteNotesByIds(context, selectedOrSingleNoteIds(id));
                clearNoteSelection();
            }
        }
    }

    const float toolbarBottom = kUiTitleHeight + kOuterPadding + kToolbarHeight;
    ImGui::SetCursorPos(ImVec2(kOuterPadding, toolbarBottom + 4.0f));
    drawHorizontalPaneSeparator(theme, std::max(1.0f, ImGui::GetWindowWidth() - kOuterPadding * 2.0f));

    if (!gNotesPanel.statusMessage.empty() && ImGui::GetTime() < gNotesPanel.statusUntil) {
        ImGui::SetCursorPos(ImVec2(kOuterPadding, toolbarBottom + 8.0f));
        ImGui::TextDisabled("%s", gNotesPanel.statusMessage.c_str());
    }

    ImGui::SetCursorPos(ImVec2(kOuterPadding, toolbarBottom + 14.0f));
    const float bodyHeight = std::max(220.0f, ImGui::GetWindowHeight() - ImGui::GetCursorPosY() - kOuterPadding);
    const float bodyWidth = std::max(420.0f, ImGui::GetWindowWidth() - kOuterPadding * 2.0f);
    const float treeWidth = std::clamp(bodyWidth * 0.24f, kTreeWidthMin, kTreeWidthMax);
    const float outlineWidth = gNotesPanel.outline.showOutline ? std::clamp(bodyWidth * 0.18f, kOutlineWidthMin, kOutlineWidthMax) : 0.0f;
    // Column gap includes a 1px separator line between panes.
    const float columnGaps = kPaneGap + (gNotesPanel.outline.showOutline ? kPaneGap : 0.0f);
    const float editorWidth = std::max(180.0f, bodyWidth - treeWidth - outlineWidth - columnGaps);

    // Flatten managed-window ChildBg so nested panes do not stack as boxes.
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    pushPaneStyle(theme);
    ImGui::BeginChild("notes-directory", ImVec2(treeWidth, bodyHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    drawNotesDirectory(context, theme);
    ImGui::EndChild();
    popPaneStyle();

    ImGui::SameLine(0.0f, (kPaneGap - 1.0f) * 0.5f);
    drawVerticalPaneSeparator(theme, bodyHeight);
    ImGui::SameLine(0.0f, (kPaneGap - 1.0f) * 0.5f);

    pushPaneStyle(theme);
    ImGui::BeginChild("notes-editor", ImVec2(editorWidth, bodyHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    selected = context.notes.find(context.runtime().selectedNoteId);
    if (selected) {
        if (!gNotesPanel.dirty) {
            gNotesPanel.draftPinned = selected->pinned;
            gNotesPanel.draftFixed = selected->fixed;
        }
        drawEditor(context, theme, *selected);
    } else {
        ImGui::TextDisabled("%s", tr("No notes"));
        ImGui::Dummy(ImVec2(1.0f, 8.0f));
        if (notesButton(theme, Icons::Add, tr("New Note"))) {
            createNoteInFolder(context, gNotesPanel.selectedFolder);
        }
    }
    ImGui::EndChild();
    popPaneStyle();

    if (gNotesPanel.outline.showOutline) {
        ImGui::SameLine(0.0f, (kPaneGap - 1.0f) * 0.5f);
        drawVerticalPaneSeparator(theme, bodyHeight);
        ImGui::SameLine(0.0f, (kPaneGap - 1.0f) * 0.5f);
        pushPaneStyle(theme);
        ImGui::BeginChild("notes-outline", ImVec2(outlineWidth, bodyHeight), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
        drawMarkdownOutlinePanel(theme, gNotesPanel.draftBody, gNotesPanel.outline);
        ImGui::EndChild();
        popPaneStyle();
    }
    ImGui::PopStyleColor();

    drawRenamePopup(context, theme);
    drawNewFolderPopup(context, theme);
    drawCloseConfirmPopup(context, theme);

    ui_anim::popAppearAlpha();
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace launcher
