#include "core/NotesStore.hpp"

#include <windows.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <unordered_set>
#include <chrono>
#include <cctype>
#include <functional>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <system_error>
#include <utility>

namespace launcher {
namespace {

using json = nlohmann::json;

constexpr int kNotesSchemaVersion = 2;
constexpr const char* kNotesIndexFileName = "index.json";
constexpr const char* kAttachmentsDirectoryName = "attachments";

std::int64_t nowUnix()
{
    return static_cast<std::int64_t>(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
}

std::string trim(std::string value)
{
    auto notSpace = [](unsigned char ch) {
        return !std::isspace(ch);
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::string readFileText(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool writeFileAtomic(const std::filesystem::path& path, const std::string& content)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return false;
    }

    const std::filesystem::path tmpPath = path.parent_path() / (path.filename().string() + ".tmp");
    {
        std::ofstream output(tmpPath, std::ios::binary);
        if (!output) {
            return false;
        }
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        if (!output) {
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
    }

    if (!MoveFileExW(tmpPath.wstring().c_str(), path.wstring().c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
    return true;
}

void setError(std::string* error, const std::string& message)
{
    if (error) {
        *error = message;
    }
}

std::string firstMarkdownTitle(const std::string& body)
{
    std::istringstream input(body);
    std::string line;
    while (std::getline(input, line)) {
        line = trim(std::move(line));
        if (!line.empty()) {
            if (line.starts_with("#")) {
                size_t pos = 0;
                while (pos < line.size() && line[pos] == '#') {
                    ++pos;
                }
                if (pos < line.size() && std::isspace(static_cast<unsigned char>(line[pos]))) {
                    return trim(line.substr(pos + 1));
                }
            }
            return line;
        }
    }
    return {};
}

json noteToJson(const Note& note)
{
    return json{{"id", note.id},
                {"title", note.title},
                {"folder", note.folder},
                {"tags", note.tags},
                {"pinned", note.pinned},
                {"fixed", note.fixed},
                {"archived", note.archived},
                {"createdAt", note.createdAt},
                {"updatedAt", note.updatedAt},
                {"markdownPath", note.markdownPath.generic_string()}};
}

Note noteFromJson(const json& j)
{
    Note note;
    note.id = j.value("id", "");
    note.title = j.value("title", "");
    note.folder = NotesStore::normalizeFolderPath(j.value("folder", ""));
    note.tags = j.value("tags", std::vector<std::string>{});
    note.pinned = j.value("pinned", false);
    note.fixed = j.value("fixed", false);
    if (note.fixed) {
        note.pinned = false;
    }
    note.archived = j.value("archived", false);
    note.createdAt = j.value("createdAt", 0LL);
    note.updatedAt = j.value("updatedAt", 0LL);
    note.markdownPath = j.value("markdownPath", "");
    if (note.folder.empty() && !note.markdownPath.empty()) {
        const std::filesystem::path relative = note.markdownPath;
        if (relative.has_parent_path()) {
            note.folder = NotesStore::normalizeFolderPath(relative.parent_path().generic_string());
        }
    }
    return note;
}

bool folderSegmentValid(const std::string& segment)
{
    if (segment.empty() || segment == "." || segment == "..") {
        return false;
    }
    for (unsigned char ch : segment) {
        if (ch < 32 || ch == '<' || ch == '>' || ch == ':' || ch == '"' || ch == '|' || ch == '?' || ch == '*') {
            return false;
        }
    }
    return true;
}

std::string attachmentFilePart(std::string value, std::string_view fallback)
{
    std::string result;
    result.reserve(value.size());
    for (unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_') {
            result.push_back(static_cast<char>(ch));
        } else if (!result.empty() && result.back() != '-') {
            result.push_back('-');
        }
    }
    while (!result.empty() && result.back() == '-') {
        result.pop_back();
    }
    if (result.empty()) {
        result.assign(fallback);
    }
    return result;
}

std::string lowercaseExtension(std::string extension)
{
    if (!extension.empty() && extension.front() == '.') {
        extension.erase(extension.begin());
    }
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    extension.erase(std::remove_if(extension.begin(), extension.end(),
                                   [](unsigned char ch) {
                                       return !(ch >= 'a' && ch <= 'z') && !(ch >= '0' && ch <= '9');
                                   }),
                    extension.end());
    if (extension.size() > 10) {
        extension.resize(10);
    }
    return extension;
}

} // namespace

NotesStore::NotesStore(std::filesystem::path directory)
    : directory_(std::move(directory))
{}

void NotesStore::setDirectory(std::filesystem::path directory)
{
    directory_ = std::move(directory);
}

const std::filesystem::path& NotesStore::directory() const
{
    return directory_;
}

std::filesystem::path NotesStore::indexPath() const
{
    return directory_ / kNotesIndexFileName;
}

std::filesystem::path NotesStore::attachmentsDirectory() const
{
    return directory_ / kAttachmentsDirectoryName;
}

std::filesystem::path NotesStore::createAttachmentPath(const std::string& noteId, const std::filesystem::path& suggestedFilename,
                                                       std::string* error) const
{
    if (directory_.empty() || noteId.empty() || !find(noteId)) {
        setError(error, "note not found");
        return {};
    }

    std::error_code ec;
    std::filesystem::create_directories(attachmentsDirectory(), ec);
    if (ec) {
        setError(error, ec.message());
        return {};
    }

    const std::string notePart = attachmentFilePart(noteId, "note");
    const std::string stemPart = attachmentFilePart(suggestedFilename.stem().string(), "image");
    const std::string extension = lowercaseExtension(suggestedFilename.extension().string());
    const auto timestamp =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    for (int suffix = 0; suffix < 10000; ++suffix) {
        std::string filename = notePart + "-" + std::to_string(timestamp) + "-" + stemPart;
        if (suffix > 0) {
            filename += "-" + std::to_string(suffix);
        }
        if (!extension.empty()) {
            filename += "." + extension;
        }
        const std::filesystem::path candidate = attachmentsDirectory() / filename;
        if (!std::filesystem::exists(candidate, ec)) {
            return candidate;
        }
        ec.clear();
    }
    setError(error, "could not allocate attachment filename");
    return {};
}

std::filesystem::path NotesStore::importAttachment(const std::string& noteId, const std::filesystem::path& source, std::string* error) const
{
    std::error_code ec;
    if (!std::filesystem::is_regular_file(source, ec)) {
        setError(error, ec ? ec.message() : "attachment source is not a file");
        return {};
    }
    const std::filesystem::path target = createAttachmentPath(noteId, source.filename(), error);
    if (target.empty()) {
        return {};
    }
    std::filesystem::copy_file(source, target, std::filesystem::copy_options::none, ec);
    if (ec) {
        setError(error, ec.message());
        return {};
    }
    return target;
}

std::filesystem::path NotesStore::resolveAttachmentReference(std::string_view reference) const
{
    constexpr std::string_view prefix = "attachment:";
    if (!reference.starts_with(prefix)) {
        return {};
    }
    const std::filesystem::path filename = std::string(reference.substr(prefix.size()));
    if (filename.empty() || filename.is_absolute() || filename.has_parent_path() || filename.filename() != filename || filename == "." ||
        filename == "..") {
        return {};
    }
    return attachmentsDirectory() / filename;
}

void NotesStore::ensureDirectories() const
{
    std::error_code ec;
    std::filesystem::create_directories(directory_, ec);
    std::filesystem::create_directories(attachmentsDirectory(), ec);
}

std::filesystem::path NotesStore::absoluteMarkdownPath(const Note& note) const
{
    if (note.markdownPath.empty()) {
        return {};
    }
    if (note.markdownPath.is_absolute()) {
        return note.markdownPath;
    }
    return directory_ / note.markdownPath;
}

std::string NotesStore::normalizeFolderPath(std::string path)
{
    for (char& ch : path) {
        if (ch == '\\') {
            ch = '/';
        }
    }
    std::vector<std::string> segments;
    std::string current;
    bool invalid = false;
    const auto flush = [&]() {
        if (invalid) {
            return;
        }
        const std::string segment = trim(std::move(current));
        current.clear();
        if (segment.empty() || segment == ".") {
            return;
        }
        if (segment == ".." || !folderSegmentValid(segment)) {
            invalid = true;
            segments.clear();
            return;
        }
        segments.push_back(segment);
    };
    for (char ch : path) {
        if (ch == '/') {
            flush();
        } else {
            current.push_back(ch);
        }
    }
    flush();
    if (invalid) {
        return {};
    }
    std::string result;
    for (const std::string& segment : segments) {
        if (!result.empty()) {
            result.push_back('/');
        }
        result += segment;
    }
    return result;
}

std::string NotesStore::folderParent(const std::string& path)
{
    const std::string normalized = normalizeFolderPath(path);
    const size_t slash = normalized.rfind('/');
    if (slash == std::string::npos) {
        return {};
    }
    return normalized.substr(0, slash);
}

std::string NotesStore::folderName(const std::string& path)
{
    const std::string normalized = normalizeFolderPath(path);
    const size_t slash = normalized.rfind('/');
    if (slash == std::string::npos) {
        return normalized;
    }
    return normalized.substr(slash + 1);
}

bool NotesStore::isFolderAncestor(const std::string& ancestor, const std::string& path)
{
    const std::string a = normalizeFolderPath(ancestor);
    const std::string p = normalizeFolderPath(path);
    if (a.empty() || p.empty() || a == p) {
        return false;
    }
    return p.starts_with(a + "/");
}

std::filesystem::path NotesStore::markdownPathFor(const std::string& noteId, const std::string& folder)
{
    const std::string normalized = normalizeFolderPath(folder);
    if (normalized.empty()) {
        return noteId + ".md";
    }
    return std::filesystem::path(normalized) / (noteId + ".md");
}

void NotesStore::rememberFolder(const std::string& folderPath)
{
    std::string path = normalizeFolderPath(folderPath);
    while (!path.empty()) {
        if (std::find(folders_.begin(), folders_.end(), path) == folders_.end()) {
            folders_.push_back(path);
        }
        path = folderParent(path);
    }
}

void NotesStore::rebuildFolderList()
{
    std::set<std::string> required;
    for (const std::string& folder : folders_) {
        std::string path = normalizeFolderPath(folder);
        while (!path.empty()) {
            required.insert(path);
            path = folderParent(path);
        }
    }
    for (const Note& note : notes_) {
        std::string path = normalizeFolderPath(note.folder);
        while (!path.empty()) {
            required.insert(path);
            path = folderParent(path);
        }
    }

    std::vector<std::string> ordered;
    ordered.reserve(required.size());
    std::set<std::string> seen;
    for (const std::string& folder : folders_) {
        const std::string path = normalizeFolderPath(folder);
        if (!path.empty() && required.contains(path) && seen.insert(path).second) {
            ordered.push_back(path);
        }
    }
    for (const std::string& path : required) {
        if (seen.insert(path).second) {
            ordered.push_back(path);
        }
    }
    folders_ = std::move(ordered);
}

bool NotesStore::relocateNoteFile(Note& note, const std::filesystem::path& newRelativePath, std::string* error)
{
    if (directory_.empty() || note.id.empty()) {
        setError(error, "invalid note");
        return false;
    }

    const std::filesystem::path oldPath = absoluteMarkdownPath(note);
    const std::filesystem::path newPath = directory_ / newRelativePath;
    if (oldPath == newPath) {
        note.markdownPath = newRelativePath;
        return true;
    }

    std::error_code ec;
    std::filesystem::create_directories(newPath.parent_path(), ec);
    if (ec) {
        setError(error, ec.message());
        return false;
    }

    if (std::filesystem::exists(oldPath, ec)) {
        std::filesystem::rename(oldPath, newPath, ec);
        if (ec) {
            std::filesystem::copy_file(oldPath, newPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                setError(error, ec.message());
                return false;
            }
            std::filesystem::remove(oldPath, ec);
        }
    } else {
        writeFileAtomic(newPath, note.body);
    }

    note.markdownPath = newRelativePath.generic_string();
    return true;
}

void NotesStore::load(std::string* error)
{
    notes_.clear();
    folders_.clear();
    if (directory_.empty()) {
        return;
    }
    ensureDirectories();

    std::ifstream input(indexPath(), std::ios::binary);
    if (!input) {
        return;
    }

    try {
        json document;
        input >> document;
        if (!document.is_object()) {
            if (error) {
                *error = "notes index root must be an object";
            }
            return;
        }
        const int schemaVersion = document.value("schemaVersion", 0);
        if (schemaVersion > kNotesSchemaVersion && error) {
            *error = "notes index schema is newer than this app";
        }
        if (const auto it = document.find("folders"); it != document.end() && it->is_array()) {
            for (const json& node : *it) {
                if (node.is_string()) {
                    rememberFolder(node.get<std::string>());
                }
            }
        }
        if (const auto it = document.find("notes"); it != document.end() && it->is_array()) {
            for (const json& node : *it) {
                if (node.is_object()) {
                    notes_.push_back(noteFromJson(node));
                }
            }
        }
        for (Note& note : notes_) {
            if (!note.id.empty()) {
                note.folder = normalizeFolderPath(note.folder);
                if (note.markdownPath.empty()) {
                    note.markdownPath = markdownPathFor(note.id, note.folder);
                }
                note.body = readFileText(absoluteMarkdownPath(note));
                if (note.title.empty()) {
                    note.title = firstMarkdownTitle(note.body);
                }
                rememberFolder(note.folder);
            }
        }
        notes_.erase(std::remove_if(notes_.begin(), notes_.end(),
                                    [](const Note& note) {
                                        return note.id.empty();
                                    }),
                     notes_.end());
        rebuildFolderList();
    } catch (const std::exception& ex) {
        if (error) {
            *error = ex.what();
        }
        notes_.clear();
        folders_.clear();
    }
}

void NotesStore::saveIndex() const
{
    if (directory_.empty()) {
        return;
    }
    json document;
    document["schemaVersion"] = kNotesSchemaVersion;
    document["folders"] = folders_;
    document["notes"] = json::array();
    for (const Note& note : notes_) {
        document["notes"].push_back(noteToJson(note));
    }
    writeFileAtomic(indexPath(), document.dump(2));
}

void NotesStore::saveNote(const Note& note)
{
    if (directory_.empty() || note.id.empty()) {
        return;
    }
    ensureDirectories();
    writeFileAtomic(absoluteMarkdownPath(note), note.body);
    saveIndex();
}

std::string NotesStore::nextId() const
{
    const std::int64_t timestamp = nowUnix();
    for (int suffix = 0; suffix < 10000; ++suffix) {
        std::string id = "note-" + std::to_string(timestamp);
        if (suffix > 0) {
            id += "-" + std::to_string(suffix);
        }
        if (!find(id)) {
            return id;
        }
    }
    return "note-" + std::to_string(timestamp) + "-overflow";
}

Note& NotesStore::createNote(const std::string& title, const std::string& folder)
{
    ensureDirectories();
    Note note;
    note.id = nextId();
    note.title = title.empty() ? "Untitled Note" : title;
    note.folder = normalizeFolderPath(folder);
    note.createdAt = nowUnix();
    note.updatedAt = note.createdAt;
    note.markdownPath = markdownPathFor(note.id, note.folder);
    note.body = "# " + note.title + "\n\n";
    rememberFolder(note.folder);
    notes_.insert(notes_.begin(), std::move(note));
    saveNote(notes_.front());
    return notes_.front();
}

bool NotesStore::archiveNote(const std::string& id, bool archived)
{
    if (Note* note = find(id)) {
        note->archived = archived;
        note->updatedAt = nowUnix();
        saveIndex();
        return true;
    }
    return false;
}

bool NotesStore::removeNoteFile(const std::string& id)
{
    auto it = std::find_if(notes_.begin(), notes_.end(), [&](const Note& note) {
        return note.id == id;
    });
    if (it == notes_.end()) {
        return false;
    }
    std::error_code ec;
    std::filesystem::remove(absoluteMarkdownPath(*it), ec);
    notes_.erase(it);
    rebuildFolderList();
    saveIndex();
    return !ec;
}

bool NotesStore::moveNote(const std::string& id, const std::string& folder, std::string* error)
{
    Note* note = find(id);
    if (!note) {
        setError(error, "note not found");
        return false;
    }
    const std::string targetFolder = normalizeFolderPath(folder);
    if (note->folder == targetFolder) {
        return true;
    }
    const std::filesystem::path newPath = markdownPathFor(note->id, targetFolder);
    if (!relocateNoteFile(*note, newPath, error)) {
        return false;
    }
    note->folder = targetFolder;
    note->updatedAt = nowUnix();
    rememberFolder(targetFolder);
    rebuildFolderList();
    saveIndex();
    return true;
}

bool NotesStore::moveNotes(const std::vector<std::string>& ids, const std::string& folder, std::string* error)
{
    if (ids.empty()) {
        return true;
    }
    bool any = false;
    std::string lastError;
    for (const std::string& id : ids) {
        std::string localError;
        if (moveNote(id, folder, &localError)) {
            any = true;
        } else if (!localError.empty()) {
            lastError = localError;
        }
    }
    if (!any && error && !lastError.empty()) {
        *error = lastError;
    }
    return any;
}

bool NotesStore::createFolder(const std::string& folderPath, std::string* error)
{
    const std::string path = normalizeFolderPath(folderPath);
    if (path.empty()) {
        setError(error, "invalid folder path");
        return false;
    }
    if (std::find(folders_.begin(), folders_.end(), path) != folders_.end()) {
        return true;
    }
    rememberFolder(path);
    rebuildFolderList();
    std::error_code ec;
    std::filesystem::create_directories(directory_ / path, ec);
    if (ec) {
        setError(error, ec.message());
        return false;
    }
    saveIndex();
    return true;
}

bool NotesStore::renameFolder(const std::string& from, const std::string& to, std::string* error)
{
    const std::string source = normalizeFolderPath(from);
    const std::string target = normalizeFolderPath(to);
    if (source.empty() || target.empty()) {
        setError(error, "invalid folder path");
        return false;
    }
    if (source == target) {
        return true;
    }
    if (isFolderAncestor(source, target) || target == source) {
        setError(error, "cannot move folder into itself");
        return false;
    }
    if (std::find(folders_.begin(), folders_.end(), target) != folders_.end()) {
        setError(error, "folder already exists");
        return false;
    }

    std::error_code ec;
    const std::filesystem::path sourceDir = directory_ / source;
    const std::filesystem::path targetDir = directory_ / target;
    if (std::filesystem::exists(sourceDir, ec)) {
        std::filesystem::create_directories(targetDir.parent_path(), ec);
        std::filesystem::rename(sourceDir, targetDir, ec);
        if (ec) {
            setError(error, ec.message());
            return false;
        }
    } else {
        std::filesystem::create_directories(targetDir, ec);
    }

    for (Note& note : notes_) {
        if (note.folder == source || isFolderAncestor(source, note.folder)) {
            const std::string suffix = note.folder == source ? std::string{} : note.folder.substr(source.size());
            const std::string nextFolder = target + suffix;
            const std::filesystem::path nextPath = markdownPathFor(note.id, nextFolder);
            note.folder = nextFolder;
            note.markdownPath = nextPath;
            note.updatedAt = nowUnix();
        }
    }

    std::vector<std::string> nextFolders;
    for (const std::string& folder : folders_) {
        if (folder == source || isFolderAncestor(source, folder)) {
            const std::string suffix = folder == source ? std::string{} : folder.substr(source.size());
            nextFolders.push_back(target + suffix);
        } else {
            nextFolders.push_back(folder);
        }
    }
    folders_ = std::move(nextFolders);
    rebuildFolderList();
    saveIndex();
    return true;
}

bool NotesStore::removeFolder(const std::string& folderPath, std::string* error)
{
    const std::string path = normalizeFolderPath(folderPath);
    if (path.empty()) {
        setError(error, "invalid folder path");
        return false;
    }
    const bool hasNotes = std::any_of(notes_.begin(), notes_.end(), [&](const Note& note) {
        return note.folder == path || isFolderAncestor(path, note.folder);
    });
    if (hasNotes) {
        setError(error, "folder is not empty");
        return false;
    }
    const bool hasChildren = std::any_of(folders_.begin(), folders_.end(), [&](const std::string& folder) {
        return isFolderAncestor(path, folder);
    });
    if (hasChildren) {
        setError(error, "folder has subfolders");
        return false;
    }

    folders_.erase(std::remove(folders_.begin(), folders_.end(), path), folders_.end());
    std::error_code ec;
    std::filesystem::remove(directory_ / path, ec);
    saveIndex();
    return true;
}

const std::vector<std::string>& NotesStore::folders() const
{
    return folders_;
}

std::vector<std::string> NotesStore::allFolderPaths() const
{
    return folders_;
}

std::vector<std::string> NotesStore::childFolders(const std::string& parentFolder) const
{
    const std::string parent = normalizeFolderPath(parentFolder);
    std::vector<std::string> children;
    for (const std::string& folder : folders_) {
        if (folderParent(folder) == parent) {
            children.push_back(folder);
        }
    }
    return children;
}

std::vector<Note*> NotesStore::notesInFolder(const std::string& folder)
{
    const std::string path = normalizeFolderPath(folder);
    std::vector<Note*> result;
    for (Note& note : notes_) {
        if (note.folder == path) {
            result.push_back(&note);
        }
    }
    return result;
}

std::vector<const Note*> NotesStore::notesInFolder(const std::string& folder) const
{
    const std::string path = normalizeFolderPath(folder);
    std::vector<const Note*> result;
    for (const Note& note : notes_) {
        if (note.folder == path) {
            result.push_back(&note);
        }
    }
    return result;
}

bool NotesStore::reorderNoteInFolder(const std::string& id, int targetIndexInFolder, std::string* error)
{
    // Keep single-note API consistent with multi-note reorder (display order).
    return reorderNotesInFolder({id}, targetIndexInFolder, error);
}

bool NotesStore::reorderNotesInFolder(const std::vector<std::string>& ids, int targetIndexInFolder, std::string* error)
{
    if (ids.empty()) {
        return true;
    }

    std::vector<std::string> ordered;
    ordered.reserve(ids.size());
    std::unordered_set<std::string> seen;
    for (const std::string& id : ids) {
        if (id.empty() || !seen.insert(id).second || !find(id)) {
            continue;
        }
        ordered.push_back(id);
    }
    if (ordered.empty()) {
        setError(error, "note not found");
        return false;
    }

    const Note* primary = find(ordered.front());
    if (!primary) {
        setError(error, "note not found");
        return false;
    }
    const std::string folder = primary->folder;
    for (const std::string& id : ordered) {
        Note* note = find(id);
        if (!note || note->folder == folder) {
            continue;
        }
        std::string localError;
        if (!moveNote(id, folder, &localError)) {
            if (error) {
                *error = localError.empty() ? "move failed" : localError;
            }
            return false;
        }
    }

    // targetIndexInFolder is among remaining (non-selected) notes in *display* order:
    // pinned notes first, then plain notes (same as the notes tree).
    std::unordered_set<std::string> selected(ordered.begin(), ordered.end());
    std::vector<Note> selectedNotes;
    selectedNotes.reserve(ordered.size());
    for (const std::string& id : ordered) {
        auto it = std::find_if(notes_.begin(), notes_.end(), [&](const Note& note) {
            return note.id == id;
        });
        if (it == notes_.end()) {
            continue;
        }
        selectedNotes.push_back(std::move(*it));
        notes_.erase(it);
    }
    if (selectedNotes.empty()) {
        setError(error, "note not found");
        return false;
    }

    // Remaining notes in the folder, display order (pinned then plain), mapped to absolute indices.
    std::vector<size_t> remainingDisplayIndices;
    remainingDisplayIndices.reserve(notes_.size());
    for (size_t i = 0; i < notes_.size(); ++i) {
        if (notes_[i].folder == folder && notes_[i].pinned) {
            remainingDisplayIndices.push_back(i);
        }
    }
    for (size_t i = 0; i < notes_.size(); ++i) {
        if (notes_[i].folder == folder && !notes_[i].pinned) {
            remainingDisplayIndices.push_back(i);
        }
    }

    const int insertAmongRemaining = std::clamp(targetIndexInFolder, 0, static_cast<int>(remainingDisplayIndices.size()));
    size_t absoluteInsert = notes_.size();
    if (insertAmongRemaining < static_cast<int>(remainingDisplayIndices.size())) {
        absoluteInsert = remainingDisplayIndices[static_cast<size_t>(insertAmongRemaining)];
    } else if (!remainingDisplayIndices.empty()) {
        // Append after the last remaining note in this folder (not always vector end).
        absoluteInsert = remainingDisplayIndices.back() + 1;
    } else {
        // No remaining notes in folder: insert near other folder members if any existed before.
        // Fall back to end of vector.
        absoluteInsert = notes_.size();
    }

    notes_.insert(notes_.begin() + static_cast<std::ptrdiff_t>(absoluteInsert), std::make_move_iterator(selectedNotes.begin()),
                  std::make_move_iterator(selectedNotes.end()));
    saveIndex();
    return true;
}

bool NotesStore::reorderFolderAmongSiblings(const std::string& folderPath, int targetIndexAmongSiblings, std::string* error)
{
    const std::string path = normalizeFolderPath(folderPath);
    if (path.empty()) {
        setError(error, "invalid folder path");
        return false;
    }
    const std::string parent = folderParent(path);
    std::vector<size_t> indices;
    for (size_t i = 0; i < folders_.size(); ++i) {
        if (folderParent(folders_[i]) == parent) {
            indices.push_back(i);
        }
    }
    int fromLocal = -1;
    for (int i = 0; i < static_cast<int>(indices.size()); ++i) {
        if (folders_[indices[static_cast<size_t>(i)]] == path) {
            fromLocal = i;
            break;
        }
    }
    if (fromLocal < 0) {
        setError(error, "folder not found");
        return false;
    }
    int insertLocal = std::clamp(targetIndexAmongSiblings, 0, static_cast<int>(indices.size()));
    if (fromLocal < insertLocal) {
        --insertLocal;
    }
    if (fromLocal == insertLocal) {
        return true;
    }

    std::string moved = std::move(folders_[indices[static_cast<size_t>(fromLocal)]]);
    folders_.erase(folders_.begin() + static_cast<std::ptrdiff_t>(indices[static_cast<size_t>(fromLocal)]));
    indices.clear();
    for (size_t i = 0; i < folders_.size(); ++i) {
        if (folderParent(folders_[i]) == parent) {
            indices.push_back(i);
        }
    }
    const size_t absoluteInsert =
        insertLocal >= static_cast<int>(indices.size()) ? folders_.size() : indices[static_cast<size_t>(insertLocal)];
    folders_.insert(folders_.begin() + static_cast<std::ptrdiff_t>(absoluteInsert), std::move(moved));

    // A flattened list can contain descendants before their parent (for example,
    // "A/Sub", "A", "B"). Moving only A would otherwise leave A/Sub first,
    // causing the UI tree builder to recreate A ahead of B. Re-emit a preorder
    // list so every folder and its subtree move together visually and persistently.
    const std::vector<std::string> source = folders_;
    std::vector<std::string> ordered;
    ordered.reserve(source.size());
    std::set<std::string> emitted;
    std::function<void(const std::string&)> appendChildren = [&](const std::string& parent) {
        for (const std::string& candidate : source) {
            if (folderParent(candidate) != parent || !emitted.insert(candidate).second) {
                continue;
            }
            ordered.push_back(candidate);
            appendChildren(candidate);
        }
    };
    appendChildren({});
    folders_ = std::move(ordered);
    saveIndex();
    return true;
}

Note* NotesStore::find(const std::string& id)
{
    auto it = std::find_if(notes_.begin(), notes_.end(), [&](const Note& note) {
        return note.id == id;
    });
    return it == notes_.end() ? nullptr : &*it;
}

const Note* NotesStore::find(const std::string& id) const
{
    auto it = std::find_if(notes_.begin(), notes_.end(), [&](const Note& note) {
        return note.id == id;
    });
    return it == notes_.end() ? nullptr : &*it;
}

std::vector<Note>& NotesStore::notes()
{
    return notes_;
}

const std::vector<Note>& NotesStore::notes() const
{
    return notes_;
}

std::string NotesStore::displayTitle(const Note& note)
{
    if (!note.title.empty()) {
        return note.title;
    }
    const std::string fromBody = firstMarkdownTitle(note.body);
    return fromBody.empty() ? "Untitled Note" : fromBody;
}

std::vector<std::string> parseNoteTags(const std::string& text)
{
    std::vector<std::string> tags;
    std::set<std::string> seen;
    std::string current;
    const auto flush = [&]() {
        std::string tag = trim(std::move(current));
        current.clear();
        if (tag.starts_with("#")) {
            tag.erase(tag.begin());
            tag = trim(std::move(tag));
        }
        if (!tag.empty() && seen.insert(tag).second) {
            tags.push_back(std::move(tag));
        }
    };
    for (char ch : text) {
        if (ch == ',' || ch == ';' || ch == '\n' || ch == '\t') {
            flush();
        } else {
            current.push_back(ch);
        }
    }
    flush();
    return tags;
}

std::string formatNoteTags(const std::vector<std::string>& tags)
{
    std::string text;
    for (const std::string& tag : tags) {
        if (!text.empty()) {
            text += ", ";
        }
        text += tag;
    }
    return text;
}

std::vector<NoteOutlineHeading> parseNoteOutline(const std::string& markdown)
{
    std::vector<NoteOutlineHeading> headings;
    int lineIndex = 0;
    size_t cursor = 0;
    while (cursor <= markdown.size()) {
        size_t end = markdown.find('\n', cursor);
        if (end == std::string::npos) {
            end = markdown.size();
        }
        std::string line = markdown.substr(cursor, end - cursor);
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        size_t pos = 0;
        while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
            ++pos;
        }
        int level = 0;
        while (pos + static_cast<size_t>(level) < line.size() && line[pos + static_cast<size_t>(level)] == '#') {
            ++level;
            if (level > 6) {
                break;
            }
        }
        if (level >= 1 && level <= 6) {
            const size_t titlePos = pos + static_cast<size_t>(level);
            if (titlePos < line.size() && std::isspace(static_cast<unsigned char>(line[titlePos]))) {
                std::string title = trim(line.substr(titlePos + 1));
                if (!title.empty()) {
                    headings.push_back(NoteOutlineHeading{level, lineIndex, std::move(title)});
                }
            }
        }
        if (end >= markdown.size()) {
            break;
        }
        cursor = end + 1;
        ++lineIndex;
    }
    return headings;
}

} // namespace launcher
