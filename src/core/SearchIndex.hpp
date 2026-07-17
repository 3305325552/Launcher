#pragma once

#include "core/GlobalFileItem.hpp"
#include "core/NotesStore.hpp"
#include "core/PluginManager.hpp"
#include "launcher/Models.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace launcher {

struct SearchSnapshot;

struct SearchResult {
    const Category* category = nullptr;
    const LaunchItem* item = nullptr;
    const Note* note = nullptr;
    const IndexedGlobalFileRecord* globalRecord = nullptr;
    int score = 0;
    bool globalFile = false;
    std::shared_ptr<const SearchSnapshot> owner;
    std::shared_ptr<const PluginSearchResult> pluginResult;
};

class SearchIndex {
public:
    void rebuild(const std::vector<Category>& categories, const std::vector<Note>& notes = {});
    void rebuildGlobalFiles(std::vector<GlobalFileRecord> files);
    void appendGlobalFiles(std::vector<GlobalFileRecord> files);
    void replaceGlobalRoot(std::string rootId, std::vector<GlobalFileRecord> files);
    void appendGlobalRoot(std::string rootId, std::vector<GlobalFileRecord> files);
    std::vector<SearchResult> query(const std::string& text, const AppSettings& settings) const;
    std::vector<SearchResult> query(const std::string& text, const AppSettings& settings, const std::atomic_bool* cancelled) const;
    std::uint64_t revision() const;

private:
    std::shared_ptr<const SearchSnapshot> snapshot() const;

    mutable std::mutex snapshotMutex_;
    std::shared_ptr<const SearchSnapshot> snapshot_;
    std::uint64_t revision_ = 0;
};

} // namespace launcher
