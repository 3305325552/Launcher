#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace launcher {

struct Note;
class NotesStore;

struct NoteImageInsert {
    std::filesystem::path path;
    std::string markdown;
};

bool noteImageAvailableOnClipboard();
std::optional<NoteImageInsert> chooseNoteImage(NotesStore& notes, const Note& note, std::string* error = nullptr);
std::optional<NoteImageInsert> pasteNoteImage(NotesStore& notes, const Note& note, std::string* error = nullptr);

} // namespace launcher
