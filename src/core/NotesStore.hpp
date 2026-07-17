#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace launcher {

struct Note {
    std::string id;
    std::string title;
    std::string folder;
    std::vector<std::string> tags;
    bool pinned = false;
    bool fixed = false;
    bool archived = false;
    std::int64_t createdAt = 0;
    std::int64_t updatedAt = 0;
    std::filesystem::path markdownPath;
    std::string body;
};

struct NoteOutlineHeading {
    int level = 1;
    int line = 0;
    std::string title;
};

class NotesStore {
public:
    NotesStore() = default;
    explicit NotesStore(std::filesystem::path directory);

    void setDirectory(std::filesystem::path directory);
    const std::filesystem::path& directory() const;
    std::filesystem::path indexPath() const;
    std::filesystem::path attachmentsDirectory() const;

    void load(std::string* error = nullptr);
    void saveIndex() const;
    void saveNote(const Note& note);
    Note& createNote(const std::string& title = {}, const std::string& folder = {});
    bool archiveNote(const std::string& id, bool archived = true);
    bool removeNoteFile(const std::string& id);
    bool moveNote(const std::string& id, const std::string& folder, std::string* error = nullptr);
    bool moveNotes(const std::vector<std::string>& ids, const std::string& folder, std::string* error = nullptr);

    bool createFolder(const std::string& folderPath, std::string* error = nullptr);
    bool renameFolder(const std::string& from, const std::string& to, std::string* error = nullptr);
    bool removeFolder(const std::string& folderPath, std::string* error = nullptr);
    bool reorderNoteInFolder(const std::string& id, int targetIndexInFolder, std::string* error = nullptr);
    bool reorderNotesInFolder(const std::vector<std::string>& ids, int targetIndexInFolder, std::string* error = nullptr);
    bool reorderFolderAmongSiblings(const std::string& folderPath, int targetIndexAmongSiblings, std::string* error = nullptr);
    const std::vector<std::string>& folders() const;
    std::vector<std::string> allFolderPaths() const;
    std::vector<std::string> childFolders(const std::string& parentFolder) const;
    std::vector<Note*> notesInFolder(const std::string& folder);
    std::vector<const Note*> notesInFolder(const std::string& folder) const;

    Note* find(const std::string& id);
    const Note* find(const std::string& id) const;
    std::vector<Note>& notes();
    const std::vector<Note>& notes() const;

    static std::string displayTitle(const Note& note);
    static std::string normalizeFolderPath(std::string path);
    static std::string folderParent(const std::string& path);
    static std::string folderName(const std::string& path);
    static bool isFolderAncestor(const std::string& ancestor, const std::string& path);
    static std::filesystem::path markdownPathFor(const std::string& noteId, const std::string& folder);

private:
    std::filesystem::path absoluteMarkdownPath(const Note& note) const;
    void ensureDirectories() const;
    std::string nextId() const;
    void rememberFolder(const std::string& folderPath);
    void rebuildFolderList();
    bool relocateNoteFile(Note& note, const std::filesystem::path& newRelativePath, std::string* error);

    std::filesystem::path directory_;
    std::vector<Note> notes_;
    std::vector<std::string> folders_;
};

std::vector<std::string> parseNoteTags(const std::string& text);
std::string formatNoteTags(const std::vector<std::string>& tags);
std::vector<NoteOutlineHeading> parseNoteOutline(const std::string& markdown);

} // namespace launcher
