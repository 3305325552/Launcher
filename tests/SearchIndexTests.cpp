#include "core/ConfigStore.hpp"
#include "core/LaunchParameterUtils.hpp"
#include "core/LauncherService.hpp"
#include "core/ModelActions.hpp"
#include "core/SearchIndex.hpp"
#include "app/AppContext.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {

bool require(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

launcher::LaunchItem item(std::string id, std::string name, launcher::LaunchItemType type = launcher::LaunchItemType::App)
{
    launcher::LaunchItem launchItem;
    launchItem.id = std::move(id);
    launchItem.name = std::move(name);
    launchItem.type = type;
    return launchItem;
}

launcher::GlobalFileRecord globalFile(std::string path, std::string, std::string, bool directory = false, std::uint32_t modifiedTime = 0)
{
    launcher::GlobalFileRecord record;
    record.path = std::move(path);
    const size_t nameOffset = launcher::globalFileNameOffset(record.path);
    record.nameOffset = static_cast<std::uint32_t>(nameOffset);
    record.parentLength = static_cast<std::uint32_t>(launcher::globalFileParentLength(record.path, nameOffset));
    record.directory = directory;
    record.modifiedTime = modifiedTime;
    return record;
}

} // namespace

int main()
{
    launcher::Category apps;
    apps.id = "apps";
    apps.name = "Apps";
    apps.items.push_back(item("browser", "Browser"));
    apps.items.push_back(item("note-item", "Pinned Note", launcher::LaunchItemType::Note));
    apps.items.back().target = "note-alpha";
    apps.items.push_back(item("title", "Browser Title", launcher::LaunchItemType::Title));
    apps.items.push_back(item("placeholder", "Browser Placeholder", launcher::LaunchItemType::Placeholder));

    launcher::LaunchItem folder = item("folder", "Tools", launcher::LaunchItemType::VirtualFolder);
    folder.children.push_back(item("terminal", "Terminal"));
    apps.items.push_back(std::move(folder));

    launcher::SearchIndex index;
    index.rebuild({apps});

    launcher::AppSettings settings;
    bool ok = true;

    const std::vector<launcher::SearchResult> browser = index.query("browser", settings);
    ok &= require(browser.size() == 1, "title and placeholder items are excluded from search results");
    ok &= require(browser.front().item && browser.front().item->id == "browser", "app item is returned for name match");

    const std::vector<launcher::SearchResult> terminal = index.query("terminal", settings);
    ok &= require(terminal.size() == 1, "virtual-folder children are indexed");
    ok &= require(terminal.front().category && terminal.front().category->id == "apps", "child result keeps owning category");

    launcher::Category usageRanked;
    usageRanked.id = "usage";
    usageRanked.name = "Usage";
    usageRanked.items.push_back(item("unused-tool", "Tool"));
    usageRanked.items.push_back(item("used-tool", "Tool"));
    usageRanked.items.back().runCount = 1;
    launcher::SearchIndex usageIndex;
    usageIndex.rebuild({usageRanked});
    std::vector<launcher::SearchResult> usageResults = usageIndex.query("tool", settings);
    ok &= require(!usageResults.empty() && usageResults.front().item && usageResults.front().item->id == "used-tool",
                  "previously used launcher items rank above equivalent unused matches");
    settings.advancedSearch = true;
    usageResults = usageIndex.query("tool", settings);
    ok &= require(!usageResults.empty() && usageResults.front().item && usageResults.front().item->id == "used-tool",
                  "Advanced Search ranks previously used launcher items above equivalent unused matches");

    launcher::LaunchItem code = item("code", "Visual Studio Code");
    code.runCount = 12;
    launcher::LaunchItem targetOnly = item("target-only", "Target Only");
    targetOnly.target = "C:/tools/vsc.exe";
    launcher::Category ranked;
    ranked.id = "ranked";
    ranked.name = "Ranked";
    ranked.items = {code, targetOnly};
    launcher::SearchIndex rankedIndex;
    rankedIndex.rebuild({ranked});
    std::vector<launcher::GlobalFileRecord> globalFiles;
    globalFiles.push_back(globalFile("C:/Program Files/Microsoft VS Code/Visual Studio Code.exe", "Visual Studio Code",
                                     "C:/Program Files/Microsoft VS Code"));
    rankedIndex.rebuildGlobalFiles(std::move(globalFiles));
    settings.advancedSearch = true;
    settings.enableGlobalSearch = true;
    settings.searchScopeTarget = true;
    const std::vector<launcher::SearchResult> advancedRanked = rankedIndex.query("vsc", settings);
    ok &= require(advancedRanked.size() == 3, "Advanced Search keeps launcher and global file matches");
    ok &= require(advancedRanked.front().item && advancedRanked.front().item->id == "code",
                  "Advanced Search ranks launcher items above global file matches");
    const std::vector<launcher::SearchResult> launcherOnlyRanked = rankedIndex.query("qs vsc", settings);
    ok &= require(!launcherOnlyRanked.empty() && std::none_of(launcherOnlyRanked.begin(), launcherOnlyRanked.end(),
                                                              [](const launcher::SearchResult& result) {
                                                                  return result.globalFile || result.globalRecord;
                                                              }),
                  "qs prefix searches launcher items without global file results");
    ok &= require(rankedIndex.query("", settings).size() == 2, "empty search excludes global files");
    std::vector<launcher::GlobalFileRecord> appendedGlobalFiles;
    appendedGlobalFiles.push_back(globalFile("D:/Tools/Appendable Utility.exe", "Appendable Utility", "D:/Tools"));
    rankedIndex.appendGlobalFiles(std::move(appendedGlobalFiles));
    const std::vector<launcher::SearchResult> appendedRanked = rankedIndex.query("appendable", settings);
    ok &= require(!appendedRanked.empty() && appendedRanked.front().globalRecord &&
                      launcher::globalFilePath(*appendedRanked.front().globalRecord) == "D:/Tools/Appendable Utility.exe",
                  "incremental global file batches become searchable");
    std::vector<launcher::GlobalFileRecord> replacedHotFiles;
    replacedHotFiles.push_back(globalFile("C:/Users/why33/.cli-proxy-api", ".cli-proxy-api", "C:/Users/why33", true));
    rankedIndex.replaceGlobalRoot("hot:user-profile", std::move(replacedHotFiles));
    const std::vector<launcher::SearchResult> hotRootRanked = rankedIndex.query(".cli-proxy-api", settings);
    ok &= require(!hotRootRanked.empty() && hotRootRanked.front().globalRecord &&
                      launcher::globalFilePath(*hotRootRanked.front().globalRecord) == "C:/Users/why33/.cli-proxy-api",
                  "hot root replace makes newly scanned user directories searchable");
    rankedIndex.replaceGlobalRoot("hot:user-profile", {});
    const std::vector<launcher::SearchResult> removedHotRoot = rankedIndex.query(".cli-proxy-api", settings);
    ok &= require(removedHotRoot.empty(), "replacing a root with empty files removes previous root entries");
    launcher::SearchIndex globalTypeIndex;
    globalTypeIndex.rebuild({});
    std::vector<launcher::GlobalFileRecord> typedGlobalFiles;
    typedGlobalFiles.push_back(globalFile("C:/Documents/Tool.txt", "Tool", "C:/Documents"));
    typedGlobalFiles.push_back(globalFile("C:/Tools/Tool.exe", "Tool", "C:/Tools"));
    globalTypeIndex.rebuildGlobalFiles(std::move(typedGlobalFiles));
    const std::vector<launcher::SearchResult> typedGlobalRanked = globalTypeIndex.query("tool", settings);
    ok &= require(typedGlobalRanked.size() == 2 && typedGlobalRanked.front().globalRecord &&
                      launcher::globalFilePath(*typedGlobalRanked.front().globalRecord) == "C:/Tools/Tool.exe",
                  "global executable-like files rank above equally named documents");
    launcher::SearchIndex noisyPathIndex;
    noisyPathIndex.rebuild({});
    std::vector<launcher::GlobalFileRecord> noisyPathFiles;
    noisyPathFiles.push_back(globalFile("C:\\Windows\\System32\\Tool.exe", "Tool", "C:\\Windows\\System32"));
    noisyPathFiles.push_back(globalFile("D:\\Tools\\Tool.exe", "Tool", "D:\\Tools"));
    noisyPathIndex.rebuildGlobalFiles(std::move(noisyPathFiles));
    settings.globalSearchHideSystemPaths = true;
    const std::vector<launcher::SearchResult> cleanGlobalRanked = noisyPathIndex.query("tool", settings);
    ok &= require(cleanGlobalRanked.size() == 1 && cleanGlobalRanked.front().globalRecord &&
                      launcher::globalFilePath(*cleanGlobalRanked.front().globalRecord) == "D:\\Tools/Tool.exe",
                  "global system path filter handles Windows backslash paths");
    launcher::SearchIndex midNameIndex;
    midNameIndex.rebuild({});
    std::vector<launcher::GlobalFileRecord> midNameFiles;
    for (int i = 0; i < 140; ++i) {
        midNameFiles.push_back(globalFile("C:/Noise/Desk Candidate " + std::to_string(i) + ".txt", "Desk Candidate", "C:/Noise"));
    }
    midNameFiles.push_back(globalFile("D:/Apps/AlphaDesktop.exe", "AlphaDesktop", "D:/Apps"));
    midNameIndex.rebuildGlobalFiles(std::move(midNameFiles));
    const std::vector<launcher::SearchResult> midNameRanked = midNameIndex.query("desktop", settings);
    ok &=
        require(std::any_of(midNameRanked.begin(), midNameRanked.end(),
                            [](const launcher::SearchResult& result) {
                                return result.globalRecord && launcher::globalFilePath(*result.globalRecord) == "D:/Apps/AlphaDesktop.exe";
                            }),
                "global file candidates include matches inside a file name");
    launcher::SearchIndex sparseFuzzyIndex;
    sparseFuzzyIndex.rebuild({});
    std::vector<launcher::GlobalFileRecord> sparseFiles;
    sparseFiles.push_back(globalFile("D:/Docs/a-long-boring-cache.txt", "a-long-boring-cache", "D:/Docs"));
    sparseFiles.push_back(globalFile("D:/Docs/abc-report.txt", "abc-report", "D:/Docs"));
    sparseFuzzyIndex.rebuildGlobalFiles(std::move(sparseFiles));
    const std::vector<launcher::SearchResult> sparseRanked = sparseFuzzyIndex.query("abc", settings);
    ok &= require(std::none_of(sparseRanked.begin(), sparseRanked.end(),
                               [](const launcher::SearchResult& result) {
                                   return result.globalRecord &&
                                          launcher::globalFilePath(*result.globalRecord) == "D:/Docs/a-long-boring-cache.txt";
                               }),
                  "global file fuzzy matching rejects sparse low-value subsequences");
    launcher::SearchIndex pathQueryIndex;
    pathQueryIndex.rebuild({});
    std::vector<launcher::GlobalFileRecord> pathQueryFiles;
    pathQueryFiles.push_back(globalFile("C:/Users/why33/AppData/Roaming/DDNet", "DDNet", "C:/Users/why33/AppData/Roaming", true));
    pathQueryFiles.push_back(globalFile("C:/Users/why33/AppData/Local/DDNet", "DDNet", "C:/Users/why33/AppData/Local", true));
    pathQueryFiles.push_back(globalFile("D:/Games/Roaming Kit/ddnet-notes.txt", "ddnet-notes", "D:/Games/Roaming Kit"));
    pathQueryFiles.push_back(globalFile("D:/Environment/Msys2/ucrt64/share/man/man3/BIO_ADDR_hostname_string.3ossl.gz",
                                        "BIO_ADDR_hostname_string.3ossl.gz", "D:/Environment/Msys2/ucrt64/share/man/man3"));
    pathQueryIndex.rebuildGlobalFiles(std::move(pathQueryFiles));
    settings.searchScopeTarget = false;
    const auto hasRoamingDdnet = [](const std::vector<launcher::SearchResult>& results) {
        return std::any_of(results.begin(), results.end(), [](const launcher::SearchResult& result) {
            return result.globalRecord && launcher::globalFilePath(*result.globalRecord) == "C:/Users/why33/AppData/Roaming/DDNet";
        });
    };
    ok &=
        require(hasRoamingDdnet(pathQueryIndex.query("roaming/ddnet", settings)), "global path search matches forward-slash path segments");
    ok &= require(hasRoamingDdnet(pathQueryIndex.query("roaming\\ddnet", settings)), "global path search matches backslash path segments");
    ok &= require(hasRoamingDdnet(pathQueryIndex.query("roaming ddnet", settings)), "global path search matches multi-term path queries");
    const std::vector<launcher::SearchResult> pathMultiTermRanked = pathQueryIndex.query("roaming ddnet", settings);
    ok &= require(!pathMultiTermRanked.empty() && pathMultiTermRanked.front().globalRecord &&
                      launcher::globalFilePath(*pathMultiTermRanked.front().globalRecord) == "C:/Users/why33/AppData/Roaming/DDNet",
                  "multi-term global path search ranks the direct path match first");
    launcher::SearchIndex directoryScopedIndex;
    directoryScopedIndex.rebuild({apps});
    std::vector<launcher::GlobalFileRecord> directoryScopedFiles;
    directoryScopedFiles.push_back(globalFile("D:/Work/Project/bin/tool.exe", "tool", "D:/Work/Project/bin"));
    directoryScopedFiles.push_back(globalFile("D:/Work/Project/readme.md", "readme", "D:/Work/Project"));
    directoryScopedFiles.push_back(globalFile("D:/Work Other/Project/bin/tool.exe", "tool", "D:/Work Other/Project/bin"));
    directoryScopedFiles.push_back(globalFile("D:/Work With Space/Project/bin/tool.exe", "tool", "D:/Work With Space/Project/bin"));
    directoryScopedIndex.rebuildGlobalFiles(std::move(directoryScopedFiles));
    const std::vector<launcher::SearchResult> scopedToolResults = directoryScopedIndex.query("dir D:/Work/Project tool", settings);
    ok &= require(!scopedToolResults.empty() &&
                      std::all_of(scopedToolResults.begin(), scopedToolResults.end(),
                                  [](const launcher::SearchResult& result) {
                                      return result.globalRecord &&
                                             launcher::globalFilePath(*result.globalRecord).starts_with("D:/Work/Project/");
                                  }),
                  "dir prefix limits global search results to the requested directory");
    ok &= require(std::none_of(scopedToolResults.begin(), scopedToolResults.end(),
                               [](const launcher::SearchResult& result) {
                                   return result.item || result.note;
                               }),
                  "dir prefix searches only global files");
    const std::vector<launcher::SearchResult> quotedScopedResults =
        directoryScopedIndex.query("dir \"D:/Work With Space/Project\" tool", settings);
    ok &= require(!quotedScopedResults.empty() && quotedScopedResults.front().globalRecord &&
                      launcher::globalFilePath(*quotedScopedResults.front().globalRecord) == "D:/Work With Space/Project/bin/tool.exe",
                  "dir prefix supports quoted directories with spaces");
    const std::vector<launcher::SearchResult> scopedDirectoryResults = directoryScopedIndex.query("dir D:/Work/Project", settings);
    ok &= require(scopedDirectoryResults.size() == 2 &&
                      std::all_of(scopedDirectoryResults.begin(), scopedDirectoryResults.end(),
                                  [](const launcher::SearchResult& result) {
                                      return result.globalRecord &&
                                             launcher::globalFilePath(*result.globalRecord).starts_with("D:/Work/Project/");
                                  }),
                  "dir prefix can list indexed files under a directory without a keyword");
    launcher::SearchIndex modifiedTimeIndex;
    modifiedTimeIndex.rebuild({});
    std::vector<launcher::GlobalFileRecord> modifiedFiles;
    modifiedFiles.push_back(globalFile("C:/Old/Report.txt", "Report", "C:/Old", false, 100));
    modifiedFiles.push_back(globalFile("C:/New/Report.txt", "Report", "C:/New", false, 200));
    modifiedTimeIndex.rebuildGlobalFiles(std::move(modifiedFiles));
    const std::vector<launcher::SearchResult> modifiedRanked = modifiedTimeIndex.query("report", settings);
    ok &= require(modifiedRanked.size() == 2 && modifiedRanked.front().globalRecord &&
                      launcher::globalFilePath(*modifiedRanked.front().globalRecord) == "C:/New/Report.txt",
                  "global files use modified time as a stable tie-breaker");
    const launcher::LaunchItem rootFileItem = launcher::makeGlobalFileItem(globalFile("C:/RootTool.exe", "RootTool.exe", "C:/"));
    ok &= require(rootFileItem.subtitle == "C:/", "root-level global files keep the drive root as parent path");
    launcher::Category manyMatches;
    manyMatches.id = "many";
    manyMatches.name = "Many";
    for (int i = 0; i < 600; ++i) {
        launcher::LaunchItem manyItem = item("tool-" + std::to_string(i), i == 599 ? "tool" : "Tool " + std::to_string(i));
        manyItem.runCount = i;
        manyMatches.items.push_back(std::move(manyItem));
    }
    launcher::SearchIndex cappedIndex;
    cappedIndex.rebuild({manyMatches});
    settings.searchResultLimit = 20;
    const std::vector<launcher::SearchResult> cappedResults = cappedIndex.query("tool", settings);
    ok &= require(cappedResults.size() == 600, "search result page size does not cap core candidates");
    ok &= require(cappedResults.front().item && cappedResults.front().item->id == "tool-599",
                  "core candidates keep the highest ranked match");
    settings.advancedSearch = false;
    settings.enableGlobalSearch = false;
    settings.searchScopeTarget = false;

    launcher::Category copied = apps;
    copied.items.clear();
    ok &= require(index.query("terminal", settings).size() == 1, "index owns a snapshot after rebuild");

    launcher::Note note;
    note.id = "note-alpha";
    note.title = "Alpha Notes";
    note.tags = {"dev", "markdown"};
    note.body = "# Alpha Notes\n\nDocker compose restart";
    note.createdAt = 100;
    note.updatedAt = 200;
    launcher::SearchIndex noteIndex;
    noteIndex.rebuild({apps}, {note});
    const std::vector<launcher::SearchResult> noteResults = noteIndex.query("docker", settings);
    ok &= require(noteResults.size() == 1 && noteResults.front().note && noteResults.front().note->id == "note-alpha",
                  "notes are indexed by markdown body");
    const std::vector<launcher::SearchResult> noteOnlyResults = noteIndex.query("note terminal", settings);
    ok &= require(std::none_of(noteOnlyResults.begin(), noteOnlyResults.end(),
                               [](const launcher::SearchResult& result) {
                                   return result.item || result.globalRecord;
                               }),
                  "note prefix searches notes without launcher item results");
    note.archived = true;
    noteIndex.rebuild({apps}, {note});
    ok &= require(noteIndex.query("docker", settings).empty(), "archived notes are excluded from search results");

    const std::filesystem::path configPath = std::filesystem::temp_directory_path() / "launcher-configstore-test" / "config.json";
    std::error_code ec;
    std::filesystem::remove_all(configPath.parent_path(), ec);

    launcher::PersistedState saved;
    saved.settings.language = "en-US";
    saved.settings.themeId = "builtin:light";
    saved.settings.enableSearchHotkey = true;
    saved.settings.searchHotkey = "Ctrl+Shift+F";
    saved.settings.advancedSearch = true;
    saved.settings.enableGlobalSearch = true;
    saved.settings.globalSearchHideSystemPaths = false;
    saved.settings.directorySearchContextMenu = false;
    saved.settings.searchResultLimit = 64;
    saved.settings.globalSearchResultLimit = 32;
    launcher::ScheduledTask savedTask;
    savedTask.id = "task-alpha";
    savedTask.name = "Morning Browser";
    savedTask.itemId = "browser";
    savedTask.trigger = launcher::ScheduledTriggerKind::Daily;
    savedTask.hour = 8;
    savedTask.minute = 30;
    savedTask.history.push_back({100, 101, true, "completed"});
    saved.scheduledTasks.push_back(savedTask);
    saved.categories.push_back(apps);

    launcher::ConfigStore store(configPath);
    store.save(saved);
    const launcher::PersistedState loaded = store.loadPersisted();

    ok &= require(loaded.settings.language == "en-US", "ConfigStore persists settings");
    ok &= require(loaded.settings.themeId == "builtin:light", "ConfigStore persists selected theme id");
    ok &= require(loaded.settings.enableSearchHotkey, "ConfigStore persists search hotkey enablement");
    ok &= require(loaded.settings.searchHotkey == "Ctrl+Shift+F", "ConfigStore persists search hotkey");
    ok &= require(loaded.settings.advancedSearch, "ConfigStore persists Advanced Search mode");
    ok &= require(loaded.settings.enableGlobalSearch, "ConfigStore persists Global Search mode");
    ok &= require(!loaded.settings.globalSearchHideSystemPaths, "ConfigStore persists Global Search system path filter");
    ok &= require(!loaded.settings.directorySearchContextMenu, "ConfigStore persists Explorer directory search menu setting");
    ok &= require(loaded.settings.searchResultLimit == 64, "ConfigStore persists search result limit");
    ok &= require(loaded.settings.globalSearchResultLimit == 32, "ConfigStore persists global file result limit");
    ok &= require(loaded.scheduledTasks.size() == 1 && loaded.scheduledTasks.front().id == "task-alpha" &&
                      loaded.scheduledTasks.front().hour == 8 && loaded.scheduledTasks.front().history.size() == 1,
                  "ConfigStore persists scheduled tasks");
    ok &= require(loaded.categories.size() == 1 && loaded.categories.front().id == "apps", "ConfigStore persists categories");
    ok &= require(std::any_of(loaded.categories.front().items.begin(), loaded.categories.front().items.end(),
                              [](const launcher::LaunchItem& savedItem) {
                                  return savedItem.id == "note-item" && savedItem.type == launcher::LaunchItemType::Note &&
                                         savedItem.target.string() == "note-alpha";
                              }),
                  "ConfigStore persists note launcher items");
    ok &= require(!launcher::LauncherService{}.launch(loaded.categories.front().items[1]),
                  "LauncherService does not shell-launch note items");

    const std::filesystem::path configLocationPath = configPath.parent_path() / "config-location.json";
    const std::filesystem::path movedConfigDirectory = configPath.parent_path() / "moved-config";
    std::string moveError;
    ok &= require(store.moveConfigDirectory(movedConfigDirectory, &moveError), "ConfigStore moves config directory");
    ok &= require(store.path() == movedConfigDirectory / "config.json", "ConfigStore updates active config path after directory move");
    ok &= require(std::filesystem::exists(store.path()), "ConfigStore copies config file into moved directory");
    ok &= require(std::filesystem::exists(configLocationPath), "ConfigStore records moved config directory");
    const launcher::PersistedState movedLoaded = store.loadPersisted();
    ok &= require(movedLoaded.categories.size() == 1 && movedLoaded.categories.front().id == "apps",
                  "ConfigStore loads data from moved config directory");
    ok &= require(store.moveConfigDirectory(configPath.parent_path(), &moveError), "ConfigStore moves config directory back to default");
    ok &= require(store.path() == configPath, "ConfigStore restores default config path");
    ok &= require(!std::filesystem::exists(configLocationPath), "ConfigStore removes location override for default directory");

    launcher::NotesStore notesStore(configPath.parent_path() / "notes");
    launcher::Note& createdNote = notesStore.createNote("Stored Note");
    const std::string createdNoteId = createdNote.id;
    createdNote.tags = {"persisted"};
    createdNote.body = "# Stored Note\n\nSaved markdown body";
    notesStore.saveNote(createdNote);
    const std::filesystem::path attachmentSource = configPath.parent_path() / "sample image.PNG";
    {
        std::ofstream attachment(attachmentSource, std::ios::binary);
        attachment << "test-image";
    }
    std::string attachmentError;
    const std::filesystem::path importedAttachment = notesStore.importAttachment(createdNoteId, attachmentSource, &attachmentError);
    ok &= require(!importedAttachment.empty() && std::filesystem::exists(importedAttachment), "NotesStore imports note attachments");
    ok &= require(importedAttachment.parent_path() == notesStore.attachmentsDirectory() && importedAttachment.extension() == ".png",
                  "NotesStore stores attachments under a stable normalized filename");
    const std::string attachmentReference = "attachment:" + importedAttachment.filename().string();
    ok &= require(notesStore.resolveAttachmentReference(attachmentReference) == importedAttachment,
                  "NotesStore resolves attachment references");
    ok &=
        require(notesStore.resolveAttachmentReference("attachment:../config.json").empty(), "NotesStore rejects attachment path traversal");
    ok &= require(notesStore.createFolder("Projects/Work"), "NotesStore creates nested folders");
    const std::string folderNoteId = notesStore.createNote("Folder Note", "Projects/Work").id;
    const launcher::Note* folderNote = notesStore.find(folderNoteId);
    ok &= require(folderNote && folderNote->folder == "Projects/Work", "NotesStore stores note folder path");
    ok &= require(folderNote && folderNote->markdownPath.generic_string().find("Projects/Work/") != std::string::npos,
                  "NotesStore places markdown under folder");
    ok &= require(notesStore.moveNote(createdNoteId, "Projects"), "NotesStore moves note into folder");
    const launcher::Note* movedNote = notesStore.find(createdNoteId);
    ok &= require(movedNote && movedNote->folder == "Projects", "NotesStore updates moved note folder");
    const std::vector<launcher::NoteOutlineHeading> outline =
        launcher::parseNoteOutline("# Title\n\nbody\n\n## Section\n\n### Detail\n\ntext\n");
    ok &= require(outline.size() == 3, "parseNoteOutline finds ATX headings");
    ok &= require(outline[0].level == 1 && outline[0].title == "Title" && outline[0].line == 0, "outline heading H1");
    ok &= require(outline[1].level == 2 && outline[1].title == "Section" && outline[1].line == 4, "outline heading H2");
    ok &= require(outline[2].level == 3 && outline[2].title == "Detail" && outline[2].line == 6, "outline heading H3");
    ok &= require(launcher::NotesStore::normalizeFolderPath("Projects\\Work//A") == "Projects/Work/A", "folder path normalization");
    ok &= require(notesStore.createFolder("SortA/Sub"), "NotesStore creates folder subtree for reorder");
    ok &= require(notesStore.createFolder("SortB"), "NotesStore creates sibling folder for reorder");
    ok &= require(notesStore.reorderFolderAmongSiblings("SortA", 3), "NotesStore reorders a folder with descendants");
    const std::vector<std::string> sortedRootFolders = notesStore.childFolders({});
    const auto sortA = std::find(sortedRootFolders.begin(), sortedRootFolders.end(), "SortA");
    const auto sortB = std::find(sortedRootFolders.begin(), sortedRootFolders.end(), "SortB");
    ok &= require(sortA != sortedRootFolders.end() && sortB != sortedRootFolders.end() && sortB < sortA,
                  "folder reorder moves a complete subtree after its sibling");
    launcher::NotesStore loadedNotesStore(configPath.parent_path() / "notes");
    loadedNotesStore.load();
    ok &= require(loadedNotesStore.notes().size() == 2, "NotesStore loads saved note index");
    ok &= require(std::any_of(loadedNotesStore.notes().begin(), loadedNotesStore.notes().end(),
                              [](const launcher::Note& note) {
                                  return note.body.find("Saved markdown body") != std::string::npos;
                              }),
                  "NotesStore loads markdown body from note file");
    ok &= require(std::find(loadedNotesStore.folders().begin(), loadedNotesStore.folders().end(), "Projects/Work") !=
                      loadedNotesStore.folders().end(),
                  "NotesStore persists folder list");
    const std::vector<std::string> reloadedRootFolders = loadedNotesStore.childFolders({});
    const auto reloadedSortA = std::find(reloadedRootFolders.begin(), reloadedRootFolders.end(), "SortA");
    const auto reloadedSortB = std::find(reloadedRootFolders.begin(), reloadedRootFolders.end(), "SortB");
    ok &= require(reloadedSortA != reloadedRootFolders.end() && reloadedSortB != reloadedRootFolders.end() && reloadedSortB < reloadedSortA,
                  "folder subtree reorder persists after reload");

    const std::filesystem::path badConfigPath = configPath.parent_path() / "bad.json";
    {
        std::ofstream badConfig(badConfigPath, std::ios::binary);
        badConfig << "{not-json";
    }
    std::string loadError;
    ok &= require(!launcher::ConfigStore(badConfigPath).tryLoadPersisted(&loadError), "ConfigStore reports invalid JSON");
    ok &= require(!loadError.empty(), "invalid JSON exposes an error message");

    const std::filesystem::path legacyLikeConfigPath = configPath.parent_path() / "legacy-like.json";
    launcher::PersistedState legacyLike;
    launcher::Category legacyCommon;
    legacyCommon.id = "common";
    legacyCommon.name = "Common";
    legacyCommon.items.push_back(item("legacy-app", "Legacy App"));
    legacyLike.categories.push_back(std::move(legacyCommon));
    launcher::ConfigStore(legacyLikeConfigPath).save(legacyLike);

    launcher::AppContext legacyContext;
    legacyContext.config = launcher::ConfigStore(legacyLikeConfigPath);
    legacyContext.load();
    ok &= require(legacyContext.persisted().categories.size() == 1, "legacy common category is preserved during load");
    ok &= require(legacyContext.persisted().categories.front().id == "common", "legacy common category id is not rewritten to seed data");
    ok &= require(legacyContext.persisted().categories.front().items.size() == 1, "legacy common category items are preserved");

    launcher::PersistedState duplicateIds;
    launcher::Category duplicateCategory;
    duplicateCategory.id = "duplicates";
    duplicateCategory.name = "Duplicates";
    duplicateCategory.items.push_back(item("same", "One"));
    duplicateCategory.items.push_back(item("same", "Two"));
    duplicateCategory.items.push_back(item("", "Three"));
    duplicateIds.categories.push_back(std::move(duplicateCategory));
    ok &= require(launcher::model_actions::ensureUniqueIds(duplicateIds), "duplicate and empty item ids are repaired");
    ok &= require(duplicateIds.categories.front().items[0].id == "same", "first unique id is preserved");
    ok &= require(duplicateIds.categories.front().items[1].id != "same", "duplicate id is replaced");
    ok &= require(!duplicateIds.categories.front().items[2].id.empty(), "empty id is replaced");

    launcher::PersistedState categoryOrder;
    categoryOrder.categories = {{"one", "One", "", {}}, {"two", "Two", "", {}}, {"three", "Three", "", {}}};
    launcher::RuntimeState categoryRuntime;
    categoryRuntime.selectedCategory = 1;
    ok &= require(launcher::model_actions::reorderCategory(categoryOrder, categoryRuntime, 0, 3), "category reorder action moves item");
    ok &= require(categoryOrder.categories[0].id == "two" && categoryOrder.categories[2].id == "one", "category order is updated");
    ok &= require(categoryRuntime.selectedCategory == 0, "selected category follows the same category id");

    std::vector<launcher::LaunchItem> reorderedItems = {item("a", "A"), item("b", "B"), item("c", "C")};
    ok &= require(launcher::model_actions::reorderItem(reorderedItems, settings, false, 0, 3), "item reorder action moves item");
    ok &= require(reorderedItems[0].id == "b" && reorderedItems[2].id == "a", "item order is updated");
    settings.sortMode = launcher::SortMode::Name;
    ok &= require(!launcher::model_actions::reorderItem(reorderedItems, settings, false, 0, 2), "sorted items are not manually reordered");
    settings.sortMode = launcher::SortMode::Free;

    launcher::PersistedState moveState;
    launcher::Category sourceCategory;
    sourceCategory.id = "source";
    sourceCategory.name = "Source";
    sourceCategory.items = {item("a", "A"), item("b", "B")};
    launcher::Category destinationCategory;
    destinationCategory.id = "destination";
    destinationCategory.name = "Destination";
    destinationCategory.items = {item("x", "X")};
    moveState.categories = {std::move(sourceCategory), std::move(destinationCategory)};
    launcher::model_actions::MoveItemsResult singleMove =
        launcher::model_actions::moveItemByIdToList(moveState, "b", moveState.categories[1].items, 0);
    ok &= require(singleMove.changed && singleMove.movedAcrossLists, "single item move reports cross-list changes");
    ok &= require(moveState.categories[0].items.size() == 1 && moveState.categories[0].items[0].id == "a",
                  "single item move removes item from source");
    ok &= require(moveState.categories[1].items.size() == 2 && moveState.categories[1].items[0].id == "b",
                  "single item move inserts item at destination index");

    launcher::PersistedState batchMoveState;
    batchMoveState.categories = {{"batch", "Batch", "", {item("a", "A"), item("b", "B"), item("c", "C"), item("d", "D")}}};
    launcher::model_actions::MoveItemsResult batchMove =
        launcher::model_actions::moveItemIdsToList(batchMoveState, {"d", "b"}, batchMoveState.categories[0].items, 1);
    ok &= require(batchMove.changed && !batchMove.movedAcrossLists, "batch same-list move reports local changes");
    ok &= require(batchMove.movedIds.size() == 2 && batchMove.movedIds[0] == "d" && batchMove.movedIds[1] == "b",
                  "batch move preserves requested selection order");
    ok &= require(batchMoveState.categories[0].items[0].id == "a" && batchMoveState.categories[0].items[1].id == "b" &&
                      batchMoveState.categories[0].items[2].id == "d" && batchMoveState.categories[0].items[3].id == "c",
                  "batch same-list move preserves display order for moved items");

    launcher::PersistedState folderMoveState;
    launcher::LaunchItem folderTarget = item("folder-target", "Folder", launcher::LaunchItemType::VirtualFolder);
    folderMoveState.categories = {{"folder-move", "Folder Move", "", {std::move(folderTarget), item("tool", "Tool")}}};
    launcher::model_actions::MoveItemsResult folderMove =
        launcher::model_actions::moveItemByIdToList(folderMoveState, "tool", folderMoveState.categories[0].items[0].children);
    ok &= require(folderMove.changed && folderMove.movedAcrossLists, "moving an item into a virtual folder is a cross-list move");
    ok &= require(folderMoveState.categories[0].items.size() == 1 && folderMoveState.categories[0].items[0].children.size() == 1 &&
                      folderMoveState.categories[0].items[0].children[0].id == "tool",
                  "item is moved into virtual folder children");

    launcher::PersistedState cycleMoveState;
    launcher::LaunchItem parentFolder = item("parent-folder", "Parent", launcher::LaunchItemType::VirtualFolder);
    parentFolder.children.push_back(item("child-folder", "Child", launcher::LaunchItemType::VirtualFolder));
    cycleMoveState.categories = {{"cycle", "Cycle", "", {std::move(parentFolder)}}};
    launcher::model_actions::MoveItemsResult cycleMove = launcher::model_actions::moveItemByIdToList(
        cycleMoveState, "parent-folder", cycleMoveState.categories[0].items[0].children[0].children);
    ok &= require(!cycleMove.changed, "moving a folder into its own descendant is rejected");
    ok &= require(cycleMoveState.categories[0].items.size() == 1 && cycleMoveState.categories[0].items[0].id == "parent-folder",
                  "rejected descendant move leaves source in place");

    launcher::LaunchItem searchTemplate = item("search-template", "Search Template");
    searchTemplate.target = "https://example.test/?q=%so-url%";
    searchTemplate.arguments = "--query=%so%";
    const launcher::LaunchItem resolvedSearch = launcher::launch_params::withSearchVariables(searchTemplate, "alpha beta");
    ok &= require(resolvedSearch.target.string() == "https://example.test/?q=alpha+beta", "search URL variables are encoded");
    ok &= require(resolvedSearch.arguments == "--query=alpha beta", "plain search variables preserve text");

    launcher::InteractiveParam numberParam;
    numberParam.kind = launcher::InteractiveParamKind::Number;
    numberParam.defaultValue = "150";
    numberParam.minValue = 0.0;
    numberParam.maxValue = 100.0;
    ok &= require(launcher::launch_params::defaultParamValue(numberParam) == "100", "numeric parameter defaults are clamped");

    launcher::LaunchItem interactiveTemplate = item("interactive", "Interactive");
    interactiveTemplate.interactive = true;
    interactiveTemplate.target = "tool-{{profile}}";
    interactiveTemplate.arguments = "--profile=%profile%";
    launcher::InteractiveParam profileParam;
    profileParam.id = "profile";
    profileParam.defaultValue = "default";
    interactiveTemplate.interactiveParams.push_back(profileParam);
    const launcher::LaunchItem resolvedInteractive = launcher::launch_params::withInteractiveValues(interactiveTemplate, {"work"});
    ok &= require(resolvedInteractive.target.string() == "tool-work" && resolvedInteractive.arguments == "--profile=work",
                  "interactive parameter placeholders are replaced");
    ok &= require(launcher::launch_params::itemNeedsInteractivePrompt(interactiveTemplate), "interactive item requires a prompt");

    launcher::launch_params::recordInteractiveHistory(interactiveTemplate, {"work"}, 10);
    launcher::launch_params::recordInteractiveHistory(interactiveTemplate, {"work"}, 20);
    launcher::launch_params::recordInteractiveHistory(interactiveTemplate, {"personal"}, 30);
    const std::vector<launcher::launch_params::HistoryCandidate> history =
        launcher::launch_params::interactiveHistoryCandidates(interactiveTemplate.interactiveParams[0], "wo");
    ok &= require(history.size() == 2 && history[0].value == "work" && history[0].useCount == 2 && history[0].prefixMatch,
                  "interactive history is ranked and marks prefix matches");
    launcher::launch_params::removeInteractiveHistoryValue(interactiveTemplate, 0, "work");
    ok &= require(interactiveTemplate.interactiveParams[0].history.size() == 1 &&
                      interactiveTemplate.interactiveParams[0].history[0].value == "personal",
                  "interactive history values can be removed");

    std::filesystem::remove_all(configPath.parent_path(), ec);

    return ok ? 0 : 1;
}
