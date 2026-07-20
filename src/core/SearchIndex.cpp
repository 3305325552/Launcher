#include "core/SearchIndex.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <regex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace launcher {

struct GlobalFileChunk {
    std::string rootId;
    std::string namePool;
    std::string parentPool;
    std::vector<IndexedGlobalFileRecord> globalFiles;
    std::array<std::vector<std::uint32_t>, 36> nameBuckets;
    std::array<std::vector<std::uint32_t>, 36> pathBuckets;
};

struct SearchSnapshot {
    std::vector<Category> categories;
    std::vector<Note> notes;
    Category globalCategory;
    std::vector<std::shared_ptr<const GlobalFileChunk>> globalChunks;
    size_t globalFileCount = 0;
};

namespace {

constexpr int kInternalItemScoreBias = 6000;
constexpr size_t kMaxSearchResults = 2048;
constexpr size_t kMinGlobalNameResultsBeforeFullScan = 96;

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string normalizedPathText(std::string_view value)
{
    std::string result;
    result.reserve(value.size() + 2);
    result.push_back('/');
    bool previousSlash = true;
    for (unsigned char ch : value) {
        char out = ch == '\\' ? '/' : static_cast<char>(std::tolower(ch));
        if (out == '/') {
            if (previousSlash) {
                continue;
            }
            previousSlash = true;
        } else {
            previousSlash = false;
        }
        result.push_back(out);
    }
    if (!result.empty() && result.back() != '/') {
        result.push_back('/');
    }
    return result;
}

std::string pathText(const std::filesystem::path& path)
{
    const std::u8string text = path.generic_u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

std::string joinTags(const std::vector<std::string>& tags)
{
    std::string result;
    for (const std::string& tag : tags) {
        if (!result.empty()) {
            result.push_back(' ');
        }
        result += tag;
    }
    return result;
}

int bucketIndex(unsigned char ch)
{
    ch = static_cast<unsigned char>(std::tolower(ch));
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a';
    }
    if (ch >= '0' && ch <= '9') {
        return 26 + ch - '0';
    }
    return -1;
}

int bestBucketIndexForQuery(const std::array<std::vector<std::uint32_t>, 36>& buckets, const std::string& text)
{
    int bestBucket = -1;
    size_t bestSize = std::numeric_limits<size_t>::max();
    std::array<bool, 36> seen{};
    for (unsigned char ch : text) {
        const int index = bucketIndex(ch);
        if ((ch & 0x80) != 0) {
            return -1;
        }
        if (index < 0 || seen[static_cast<size_t>(index)]) {
            continue;
        }
        seen[static_cast<size_t>(index)] = true;
        const size_t bucketSize = buckets[static_cast<size_t>(index)].size();
        if (bucketSize == 0) {
            continue;
        }
        if (bucketSize < bestSize) {
            bestSize = bucketSize;
            bestBucket = index;
        }
    }
    return bestBucket;
}

void addNameToBuckets(std::array<std::vector<std::uint32_t>, 36>& buckets, std::string_view text, size_t index)
{
    if (index > std::numeric_limits<std::uint32_t>::max()) {
        return;
    }
    const auto storedIndex = static_cast<std::uint32_t>(index);
    std::array<bool, 36> seen{};
    for (unsigned char ch : text) {
        const int bucket = bucketIndex(ch);
        if (bucket >= 0 && !seen[static_cast<size_t>(bucket)]) {
            seen[static_cast<size_t>(bucket)] = true;
            buckets[static_cast<size_t>(bucket)].push_back(storedIndex);
        }
    }
}

void addGlobalFileToBuckets(GlobalFileChunk& chunk, size_t index)
{
    if (index >= chunk.globalFiles.size()) {
        return;
    }
    const IndexedGlobalFileRecord& item = chunk.globalFiles[index];
    addNameToBuckets(chunk.nameBuckets, globalFileName(item), index);
    addNameToBuckets(chunk.pathBuckets, globalFileParentPath(item), index);
    addNameToBuckets(chunk.pathBuckets, globalFileName(item), index);
}

std::shared_ptr<const GlobalFileChunk> buildGlobalChunk(std::vector<GlobalFileRecord> files, std::string rootId = {})
{
    auto chunk = std::make_shared<GlobalFileChunk>();
    chunk->rootId = std::move(rootId);
    size_t namePoolSize = 0;
    std::unordered_set<std::string_view> uniqueParents;
    uniqueParents.reserve(files.size());
    for (const GlobalFileRecord& file : files) {
        const std::string_view name = globalFileName(file);
        const std::string_view parent = globalFileParentPath(file);
        namePoolSize += name.size();
        if (!parent.empty()) {
            uniqueParents.insert(parent);
        }
    }
    size_t parentPoolSize = 0;
    for (std::string_view parent : uniqueParents) {
        parentPoolSize += parent.size();
    }
    chunk->namePool.reserve(namePoolSize);
    chunk->parentPool.reserve(parentPoolSize);
    chunk->globalFiles.reserve(files.size());
    std::unordered_map<std::string_view, std::string_view> internedParents;
    internedParents.reserve(uniqueParents.size());
    for (std::string_view parent : uniqueParents) {
        const size_t offset = chunk->parentPool.size();
        chunk->parentPool.append(parent);
        internedParents.emplace(parent, std::string_view(chunk->parentPool.data() + offset, parent.size()));
    }
    uniqueParents.clear();
    uniqueParents.rehash(0);
    for (const GlobalFileRecord& file : files) {
        const std::string_view name = globalFileName(file);
        const std::string_view parent = globalFileParentPath(file);
        const size_t nameOffset = chunk->namePool.size();
        chunk->namePool.append(name);
        IndexedGlobalFileRecord record;
        record.nameData = chunk->namePool.data() + nameOffset;
        record.nameLength = static_cast<std::uint32_t>(std::min<size_t>(name.size(), std::numeric_limits<std::uint32_t>::max()));
        if (!parent.empty()) {
            if (auto parentIt = internedParents.find(parent); parentIt != internedParents.end()) {
                record.parentPathData = parentIt->second.data();
                record.parentPathLength =
                    static_cast<std::uint32_t>(std::min<size_t>(parentIt->second.size(), std::numeric_limits<std::uint32_t>::max()));
            }
        }
        record.directory = file.directory;
        record.modifiedTime = file.modifiedTime;
        chunk->globalFiles.push_back(record);
    }
    internedParents.clear();
    internedParents.rehash(0);
    files.clear();
    files.shrink_to_fit();
    for (size_t i = 0; i < chunk->globalFiles.size(); ++i) {
        addGlobalFileToBuckets(*chunk, i);
    }
    for (std::vector<std::uint32_t>& bucket : chunk->nameBuckets) {
        bucket.shrink_to_fit();
    }
    for (std::vector<std::uint32_t>& bucket : chunk->pathBuckets) {
        bucket.shrink_to_fit();
    }
    return chunk;
}

bool fuzzyContains(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) {
        return true;
    }
    size_t at = 0;
    for (char ch : haystack) {
        if (at < needle.size() && ch == needle[at]) {
            ++at;
        }
    }
    return at == needle.size();
}

bool shouldSearchGlobalFiles(const std::string& needle)
{
    if (needle.empty()) {
        return false;
    }
    if (needle.size() == 1 && static_cast<unsigned char>(needle.front()) < 0x80) {
        return false;
    }
    return true;
}

bool searchCancelled(const std::atomic_bool* cancelled)
{
    return cancelled && cancelled->load(std::memory_order_relaxed);
}

std::vector<std::string> termsFrom(std::string value)
{
    std::vector<std::string> terms;
    std::string current;
    for (unsigned char ch : value) {
        if (std::isspace(ch)) {
            if (!current.empty()) {
                terms.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(static_cast<char>(ch));
        }
    }
    if (!current.empty()) {
        terms.push_back(current);
    }
    std::sort(terms.begin(), terms.end(), [](const std::string& lhs, const std::string& rhs) {
        return lhs.size() > rhs.size();
    });
    return terms;
}

std::vector<std::string> orderedTermsFrom(std::string_view value)
{
    std::vector<std::string> terms;
    std::string current;
    for (unsigned char ch : value) {
        if (std::isspace(ch)) {
            if (!current.empty()) {
                terms.push_back(current);
                current.clear();
            }
        } else {
            current.push_back(static_cast<char>(ch));
        }
    }
    if (!current.empty()) {
        terms.push_back(current);
    }
    return terms;
}

std::vector<std::string> splitPathQuerySegments(std::string_view query)
{
    std::vector<std::string> segments;
    std::string current;
    for (unsigned char ch : query) {
        if (ch == '/' || ch == '\\') {
            if (!current.empty()) {
                segments.push_back(current);
                current.clear();
            }
            continue;
        }
        if (std::isspace(ch)) {
            if (!current.empty()) {
                segments.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(static_cast<char>(ch));
    }
    if (!current.empty()) {
        segments.push_back(current);
    }
    return segments;
}

struct GlobalPathQuery {
    bool enabled = false;
    bool slashSeparated = false;
    std::string normalized;
    std::string bucketText;
    std::vector<std::string> terms;
};

struct GlobalDirectoryScope {
    bool enabled = false;
    std::string normalizedRoot;
    std::string bucketText;
};

GlobalPathQuery parseGlobalPathQuery(const std::string& needle, const std::vector<std::string>& orderedTerms)
{
    GlobalPathQuery result;
    if (needle.size() < 3) {
        return result;
    }

    result.slashSeparated = needle.find_first_of("/\\:") != std::string::npos;
    if (result.slashSeparated) {
        result.terms = splitPathQuerySegments(needle);
        result.normalized = normalizedPathText(needle);
    } else if (orderedTerms.size() >= 2) {
        result.terms = orderedTerms;
    }

    result.terms.erase(std::remove_if(result.terms.begin(), result.terms.end(),
                                      [](const std::string& term) {
                                          return term.empty();
                                      }),
                       result.terms.end());
    result.enabled = result.slashSeparated ? !result.terms.empty() : result.terms.size() >= 2;
    if (!result.enabled) {
        return result;
    }

    for (const std::string& term : result.terms) {
        result.bucketText += term;
    }
    return result;
}

std::string initials(std::string value)
{
    std::string result;
    bool next = true;
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            if (next) {
                result.push_back(static_cast<char>(std::tolower(ch)));
            }
            next = false;
        } else {
            next = true;
        }
    }
    return result;
}

bool regexMatches(const std::vector<std::string>& fields, const std::string& pattern)
{
    try {
        const std::regex expression(pattern, std::regex_constants::icase);
        return std::any_of(fields.begin(), fields.end(), [&](const std::string& field) {
            return std::regex_search(field, expression);
        });
    } catch (const std::regex_error&) {
        return false;
    }
}

bool isWordBoundary(const std::string& value, size_t index)
{
    if (index == 0 || index >= value.size()) {
        return true;
    }
    const unsigned char prev = static_cast<unsigned char>(value[index - 1]);
    const unsigned char curr = static_cast<unsigned char>(value[index]);
    return !std::isalnum(prev) || (std::isdigit(prev) && std::isalpha(curr)) || (std::isalpha(prev) && std::isdigit(curr));
}

int contiguousScore(const std::string& field, const std::string& term)
{
    if (field.empty() || term.empty()) {
        return 0;
    }

    int best = 0;
    size_t pos = field.find(term);
    while (pos != std::string::npos) {
        int score = 1180 - static_cast<int>(std::min<size_t>(pos, 160)) * 3;
        if (field.size() == term.size()) {
            score += 620;
        } else if (pos == 0) {
            score += 430;
        } else if (isWordBoundary(field, pos)) {
            score += 280;
        }
        if (pos + term.size() == field.size() || (pos + term.size() < field.size() && isWordBoundary(field, pos + term.size()))) {
            score += 60;
        }
        best = std::max(best, score);
        pos = field.find(term, pos + 1);
    }
    return best;
}

int subsequenceScore(const std::string& field, const std::string& term)
{
    if (field.empty() || term.empty() || term.size() > field.size()) {
        return 0;
    }

    size_t first = std::string::npos;
    size_t last = 0;
    size_t at = 0;
    int consecutive = 0;
    int boundaryHits = 0;
    size_t previous = std::string::npos;
    for (size_t i = 0; i < field.size() && at < term.size(); ++i) {
        if (field[i] != term[at]) {
            continue;
        }
        if (first == std::string::npos) {
            first = i;
        }
        if (previous != std::string::npos && i == previous + 1) {
            ++consecutive;
        }
        if (isWordBoundary(field, i)) {
            ++boundaryHits;
        }
        previous = i;
        last = i;
        ++at;
    }
    if (at != term.size()) {
        return 0;
    }

    const int span = static_cast<int>(last - first + 1);
    const int gaps = span - static_cast<int>(term.size());
    int score = static_cast<int>(term.size()) * 1000 - static_cast<int>(std::min<size_t>(field.size(), 240)) * 10;
    score -= static_cast<int>(std::min<size_t>(first, 180)) * 2;
    score -= gaps * 90;
    if (gaps > 0) {
        score -= 400;
    }
    score += consecutive * 70;
    score += boundaryHits * 80;
    if (first == 0) {
        score += 220;
    } else if (boundaryHits > 0) {
        score += 120;
    }
    return std::max(score, 1);
}

int termScoreInField(const std::string& field, const std::string& term)
{
    if (field.empty() || term.empty()) {
        return 0;
    }
    return std::max(contiguousScore(field, term), subsequenceScore(field, term));
}

int compactSubsequenceScore(const std::string& field, const std::string& term)
{
    if (field.empty() || term.size() < 2 || term.size() > field.size()) {
        return 0;
    }

    size_t first = std::string::npos;
    size_t last = 0;
    size_t at = 0;
    int consecutive = 0;
    int boundaryHits = 0;
    size_t previous = std::string::npos;
    for (size_t i = 0; i < field.size() && at < term.size(); ++i) {
        if (field[i] != term[at]) {
            continue;
        }
        if (first == std::string::npos) {
            first = i;
        }
        if (previous != std::string::npos && i == previous + 1) {
            ++consecutive;
        }
        if (isWordBoundary(field, i)) {
            ++boundaryHits;
        }
        previous = i;
        last = i;
        ++at;
    }
    if (at != term.size()) {
        return 0;
    }

    const size_t span = last - first + 1;
    const size_t maxSpan = std::max(term.size() * 3, term.size() + 8);
    const int gaps = static_cast<int>(span) - static_cast<int>(term.size());
    if (span > maxSpan) {
        return 0;
    }
    if (consecutive == 0 && boundaryHits == 0 && gaps > 2) {
        return 0;
    }
    if (term.size() <= 3 && boundaryHits == 0 && gaps > 4) {
        return 0;
    }
    return subsequenceScore(field, term);
}

int globalNameTermScore(const std::string& field, const std::string& term)
{
    if (field.empty() || term.empty()) {
        return 0;
    }
    return std::max(contiguousScore(field, term), compactSubsequenceScore(field, term));
}

int recencyBoost(std::int64_t lastRunAt)
{
    if (lastRunAt <= 0) {
        return 0;
    }
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    const std::int64_t ageSeconds = static_cast<std::int64_t>(now) - lastRunAt;
    if (ageSeconds < 0) {
        return 30;
    }
    constexpr std::int64_t day = 24 * 60 * 60;
    if (ageSeconds <= day) return 90;
    if (ageSeconds <= 7 * day) return 65;
    if (ageSeconds <= 30 * day) return 35;
    return 0;
}

int modifiedTimeBoost(std::uint32_t modifiedTime)
{
    if (modifiedTime == 0) {
        return 0;
    }
    return recencyBoost(static_cast<std::int64_t>(modifiedTime)) / 2;
}

int usageBoost(const LaunchItem& item)
{
    if (item.runCount <= 0 && item.lastRunAt <= 0) {
        return 0;
    }

    int score = 240;
    score += std::min(item.runCount, 10) * 28;
    score += std::min(std::max(item.runCount - 10, 0), 40) * 8;
    score += std::min(std::max(item.runCount - 50, 0), 150) * 2;
    score += recencyBoost(item.lastRunAt) * 3;
    return std::min(score, 950);
}

int noteRecencyBoost(std::int64_t updatedAt)
{
    return recencyBoost(updatedAt) * 2;
}

int typeBoost(LaunchItemType type)
{
    switch (type) {
    case LaunchItemType::App: return 140;
    case LaunchItemType::BuiltIn: return 120;
    case LaunchItemType::Url: return 95;
    case LaunchItemType::Script: return 85;
    case LaunchItemType::VirtualFolder: return 80;
    case LaunchItemType::Note: return 110;
    case LaunchItemType::Title:
    case LaunchItemType::Placeholder:
    default: return 0;
    }
}

std::string extensionOf(std::string_view path)
{
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash)) {
        return {};
    }
    return std::string(path.substr(dot));
}

int globalPathDepthPenalty(const std::string& normalizedPath)
{
    const int separators = static_cast<int>(std::count(normalizedPath.begin(), normalizedPath.end(), '/'));
    return std::min(separators * 5, 120);
}

int globalNoisyPathPenalty(const std::string& normalizedPath)
{
    static constexpr const char* kNoisySegments[] = {
        "/$recycle.bin/",
        "/.git/",
        "/appdata/local/temp/",
        "/appdata/local/packages/",
        "/appdata/local/microsoft/windows/",
        "/appdata/locallow/",
        "/node_modules/",
        "/program files/common files/",
        "/program files/windowsapps/",
        "/programdata/package cache/",
        "/programdata/microsoft/windows/",
        "/system volume information/",
        "/windows/",
        "/windows/assembly/",
        "/windows/installer/",
        "/windows/servicing/",
        "/windows/system32/driverstore/",
        "/windows/temp/",
        "/windows/winsxs/",
    };

    for (const char* segment : kNoisySegments) {
        if (normalizedPath.find(segment) != std::string::npos) {
            return 360;
        }
    }
    return 0;
}

int globalFileTypeBoost(const IndexedGlobalFileRecord& item)
{
    if (item.directory) {
        return 130;
    }

    const std::string ext = lower(extensionOf(globalFileName(item)));
    if (ext == ".exe" || ext == ".lnk" || ext == ".appref-ms" || ext == ".msc" || ext == ".cpl" || ext == ".bat" || ext == ".cmd" ||
        ext == ".ps1") {
        return 220;
    }
    if (ext == ".pdf" || ext == ".doc" || ext == ".docx" || ext == ".xls" || ext == ".xlsx" || ext == ".ppt" || ext == ".pptx" ||
        ext == ".txt" || ext == ".md") {
        return 75;
    }
    if (ext == ".zip" || ext == ".7z" || ext == ".rar") {
        return 35;
    }
    if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".mp4" || ext == ".mp3") {
        return 20;
    }
    return 0;
}

int globalFileScoreAdjustment(const IndexedGlobalFileRecord& item)
{
    const std::string normalizedPath = normalizedPathText(globalFileParentPath(item));
    int score = globalFileTypeBoost(item);
    score -= globalPathDepthPenalty(normalizedPath);
    score -= globalNoisyPathPenalty(normalizedPath);
    const std::string_view name = globalFileName(item);
    if (!name.empty() && name.front() == '.') {
        score -= 80;
    }
    score += modifiedTimeBoost(item.modifiedTime);
    return score;
}

bool isNoisyGlobalFile(const IndexedGlobalFileRecord& item)
{
    return globalNoisyPathPenalty(normalizedPathText(globalFileParentPath(item))) > 0;
}

int advancedScoreGlobalFileName(const IndexedGlobalFileRecord& item, const std::string& query, const std::vector<std::string>& terms,
                                const AppSettings& settings)
{
    if (terms.empty()) {
        return 0;
    }

    const std::string nameText(globalFileName(item));
    const std::string name = lower(nameText);
    const std::string nameInitials = settings.searchPinyinInitial ? lower(initials(nameText)) : std::string{};

    int total = 0;
    for (const std::string& term : terms) {
        int best = 0;
        const auto consider = [&](const std::string& field, int weight) {
            if (!field.empty()) {
                best = std::max(best, globalNameTermScore(field, term) * weight / 100);
            }
        };
        consider(name, 130);
        if (!nameInitials.empty()) {
            best = std::max(best, contiguousScore(nameInitials, term) * 96 / 100);
        }
        if (best <= 0) {
            return 0;
        }
        total += best;
    }

    if (name == query) {
        total += 900;
    } else if (name.starts_with(query)) {
        total += 560;
    } else if (name.find(query) != std::string::npos) {
        total += 260;
    }
    return total;
}

int advancedScoreGlobalFilePath(const IndexedGlobalFileRecord& item, const std::string& query, const std::vector<std::string>& terms)
{
    if (terms.empty()) {
        return 0;
    }

    const std::string path = lower(globalFilePath(item));
    int total = 0;
    for (const std::string& term : terms) {
        const int score = contiguousScore(path, term) * 42 / 100;
        if (score <= 0) {
            return 0;
        }
        total += score;
    }
    if (path.find(query) != std::string::npos) {
        total += 80;
    }
    return total;
}

int orderedPathTermBonus(const std::string& normalizedPath, const std::vector<std::string>& terms, bool required)
{
    size_t cursor = 0;
    int bonus = 0;
    for (const std::string& term : terms) {
        const size_t pos = normalizedPath.find(term, cursor);
        if (pos == std::string::npos) {
            return required ? 0 : bonus;
        }
        bonus += 260;
        if (isWordBoundary(normalizedPath, pos)) {
            bonus += 120;
        }
        cursor = pos + term.size();
    }
    return bonus;
}

int pathQueryTermScore(const std::string& normalizedPath, const std::string& term)
{
    const int contiguous = contiguousScore(normalizedPath, term);
    if (contiguous > 0) {
        return contiguous;
    }
    if (term.size() <= 3) {
        return compactSubsequenceScore(normalizedPath, term) / 2;
    }
    return 0;
}

int scoreGlobalPathQuery(const IndexedGlobalFileRecord& item, const GlobalPathQuery& pathQuery)
{
    if (!pathQuery.enabled || pathQuery.terms.empty()) {
        return 0;
    }

    const std::string normalizedPath = normalizedPathText(globalFilePath(item));
    int total = 0;
    if (pathQuery.slashSeparated) {
        if (!pathQuery.normalized.empty() && normalizedPath.find(pathQuery.normalized) != std::string::npos) {
            total += 3200;
        }
        const int orderedBonus = orderedPathTermBonus(normalizedPath, pathQuery.terms, true);
        if (orderedBonus <= 0) {
            return 0;
        }
        total += orderedBonus;
    } else {
        const int orderedBonus = orderedPathTermBonus(normalizedPath, pathQuery.terms, false);
        total += orderedBonus;
    }

    for (const std::string& term : pathQuery.terms) {
        const int score = pathQueryTermScore(normalizedPath, term);
        if (score <= 0) {
            return 0;
        }
        total += score * 145 / 100;
    }

    if (item.directory) {
        total += 420;
    }
    if (!pathQuery.terms.empty() && lower(std::string(globalFileName(item))) == pathQuery.terms.back()) {
        total += 950;
    }
    return total;
}

int scoreGlobalFileBase(const IndexedGlobalFileRecord& item, const std::string& needle, const std::vector<std::string>& advancedTerms,
                        const AppSettings& settings, bool pathMode, const GlobalPathQuery* pathQuery = nullptr)
{
    if (pathMode && pathQuery && pathQuery->enabled) {
        const int score = scoreGlobalPathQuery(item, *pathQuery);
        if (score > 0) {
            return settings.advancedSearch ? score : std::max(80, score / 24);
        }
    }

    if (settings.advancedSearch) {
        const int baseScore = pathMode ? advancedScoreGlobalFilePath(item, needle, advancedTerms)
                                       : advancedScoreGlobalFileName(item, needle, advancedTerms, settings);
        return baseScore > 0 && pathMode ? baseScore - 650 : baseScore;
    }

    const std::string name = lower(std::string(globalFileName(item)));
    const std::string target = pathMode ? normalizedPathText(globalFilePath(item)) : std::string{};
    const std::string normalizedNeedle = pathMode ? normalizedPathText(needle) : std::string{};

    int score = 0;
    if (name == needle)
        score = 100;
    else if (name.starts_with(needle))
        score = 80;
    else if (name.find(needle) != std::string::npos)
        score = 60;
    else if (pathMode && target.find(normalizedNeedle) != std::string::npos)
        score = 18;
    else if (settings.searchPinyinInitial && initials(std::string(globalFileName(item))).find(needle) != std::string::npos)
        score = 18;
    else if (settings.searchPinyin && name.find(needle) != std::string::npos)
        score = 16;
    else if (!pathMode && settings.searchEnglishMode && fuzzyContains(name, needle))
        score = 12;
    if (score <= 0) {
        return 0;
    }
    return score;
}

bool isInGlobalDirectoryScope(const IndexedGlobalFileRecord& item, const GlobalDirectoryScope& scope)
{
    if (!scope.enabled || scope.normalizedRoot.empty()) {
        return true;
    }

    const std::string normalizedPath = normalizedPathText(globalFilePath(item));
    return normalizedPath.starts_with(scope.normalizedRoot);
}

int advancedScoreItem(const LaunchItem& item, const std::string& query, const AppSettings& settings)
{
    if (query.empty()) {
        return 1 + typeBoost(item.type) + usageBoost(item) / 2;
    }

    const std::vector<std::string> terms = termsFrom(query);
    if (terms.empty()) {
        return 0;
    }

    struct Field {
        std::string value;
        int weight = 100;
        bool enabled = true;
    };

    std::vector<Field> fields = {
        {lower(item.name), 130, true},
        {lower(item.keywords), 118, true},
        {lower(item.subtitle), 76, true},
        {lower(item.hotkey), 68, true},
        {lower(initials(item.name)), 96, settings.searchPinyinInitial},
        {lower(initials(item.name + " " + item.keywords)), 88, settings.searchPinyinInitial},
        {lower(pathText(item.target)), 58, settings.searchScopeTarget},
        {lower(item.arguments), 50, settings.searchScopeTarget},
        {lower(item.remark), 46, settings.searchScopeRemark},
    };

    int total = 0;
    for (const std::string& term : terms) {
        int best = 0;
        for (const Field& field : fields) {
            if (!field.enabled || field.value.empty()) {
                continue;
            }
            best = std::max(best, termScoreInField(field.value, term) * field.weight / 100);
        }
        if (best <= 0) {
            return 0;
        }
        total += best;
    }

    const std::string name = lower(item.name);
    if (name == query) {
        total += 900;
    } else if (name.starts_with(query)) {
        total += 560;
    } else if (name.find(query) != std::string::npos) {
        total += 260;
    }

    total += typeBoost(item.type);
    total += usageBoost(item);
    if (!item.keywords.empty()) {
        total += 30;
    }
    return total;
}

int scoreItem(const LaunchItem& item, const std::string& needle, const AppSettings& settings)
{
    if (settings.advancedSearch) {
        return advancedScoreItem(item, needle, settings);
    }

    if (needle.empty()) {
        return 1 + typeBoost(item.type) + usageBoost(item) / 2;
    }

    const std::string name = lower(item.name);
    const std::string subtitle = lower(item.subtitle);
    const std::string keywords = lower(item.keywords);
    const std::string remark = lower(item.remark);
    const std::string hotkey = lower(item.hotkey);
    const std::string target = lower(pathText(item.target));
    const std::string arguments = lower(item.arguments);

    int score = 0;
    if (name == needle)
        score = 100;
    else if (name.starts_with(needle))
        score = 80;
    else if (name.find(needle) != std::string::npos)
        score = 60;
    else if (keywords.find(needle) != std::string::npos)
        score = 40;
    else if (hotkey.find(needle) != std::string::npos)
        score = 35;
    else if (settings.searchScopeTarget && target.find(needle) != std::string::npos)
        score = 30;
    else if (settings.searchScopeTarget && arguments.find(needle) != std::string::npos)
        score = 28;
    else if (settings.searchScopeRemark && remark.find(needle) != std::string::npos)
        score = 25;
    else if (subtitle.find(needle) != std::string::npos)
        score = 20;
    else if (settings.searchPinyinInitial && initials(item.name).find(needle) != std::string::npos)
        score = 18;
    else if (settings.searchPinyin && lower(item.name + item.keywords).find(needle) != std::string::npos)
        score = 16;
    else if (settings.searchEnglishMode && fuzzyContains(name + keywords + subtitle, needle))
        score = 12;
    if (settings.searchRegex) {
        std::vector<std::string> fields = {item.name, item.subtitle, item.keywords, item.hotkey};
        if (settings.searchScopeTarget) {
            fields.push_back(pathText(item.target));
            fields.push_back(item.arguments);
        }
        if (settings.searchScopeRemark) {
            fields.push_back(item.remark);
        }
        if (regexMatches(fields, needle)) score = std::max(score, 10);
    }
    if (score <= 0) {
        return 0;
    }
    return score + usageBoost(item);
}

int advancedScoreNote(const Note& note, const std::string& query, const AppSettings& settings)
{
    if (note.archived) {
        return 0;
    }
    if (query.empty()) {
        return 1 + (note.pinned ? 420 : 0) + noteRecencyBoost(note.updatedAt);
    }

    const std::vector<std::string> terms = termsFrom(query);
    if (terms.empty()) {
        return 0;
    }

    struct Field {
        std::string value;
        int weight = 100;
        bool enabled = true;
    };

    const std::string tags = joinTags(note.tags);
    std::vector<Field> fields = {
        {lower(NotesStore::displayTitle(note)), 135, true},
        {lower(tags), 122, true},
        {lower(initials(NotesStore::displayTitle(note) + " " + tags)), 92, settings.searchPinyinInitial},
        {lower(note.body), 58, true},
    };

    int total = 0;
    for (const std::string& term : terms) {
        int best = 0;
        for (const Field& field : fields) {
            if (!field.enabled || field.value.empty()) {
                continue;
            }
            best = std::max(best, termScoreInField(field.value, term) * field.weight / 100);
        }
        if (best <= 0) {
            return 0;
        }
        total += best;
    }

    const std::string title = lower(NotesStore::displayTitle(note));
    if (title == query) {
        total += 900;
    } else if (title.starts_with(query)) {
        total += 560;
    } else if (title.find(query) != std::string::npos) {
        total += 260;
    }
    if (note.pinned) {
        total += 420;
    }
    total += noteRecencyBoost(note.updatedAt);
    return total;
}

int scoreNote(const Note& note, const std::string& needle, const AppSettings& settings)
{
    if (settings.advancedSearch) {
        return advancedScoreNote(note, needle, settings);
    }
    if (note.archived) {
        return 0;
    }
    if (needle.empty()) {
        return 1 + (note.pinned ? 80 : 0) + noteRecencyBoost(note.updatedAt);
    }

    const std::string title = lower(NotesStore::displayTitle(note));
    const std::string tags = lower(joinTags(note.tags));
    const std::string body = lower(note.body);

    int score = 0;
    if (title == needle)
        score = 100;
    else if (title.starts_with(needle))
        score = 82;
    else if (title.find(needle) != std::string::npos)
        score = 66;
    else if (tags.find(needle) != std::string::npos)
        score = 48;
    else if (body.find(needle) != std::string::npos)
        score = 24;
    else if (settings.searchPinyinInitial &&
             initials(NotesStore::displayTitle(note) + " " + joinTags(note.tags)).find(needle) != std::string::npos)
        score = 18;
    if (settings.searchRegex) {
        std::vector<std::string> fields = {NotesStore::displayTitle(note), joinTags(note.tags), note.body};
        if (regexMatches(fields, needle)) score = std::max(score, 12);
    }
    if (score <= 0) {
        return 0;
    }
    if (note.pinned) {
        score += 80;
    }
    return score + noteRecencyBoost(note.updatedAt);
}

bool betterResult(const SearchResult& lhs, const SearchResult& rhs)
{
    if (lhs.score != rhs.score) {
        return lhs.score > rhs.score;
    }
    const std::string lhsName = lhs.item           ? lower(lhs.item->name)
                                : lhs.note         ? lower(NotesStore::displayTitle(*lhs.note))
                                : lhs.globalRecord ? lower(std::string(globalFileName(*lhs.globalRecord)))
                                                   : std::string{};
    const std::string rhsName = rhs.item           ? lower(rhs.item->name)
                                : rhs.note         ? lower(NotesStore::displayTitle(*rhs.note))
                                : rhs.globalRecord ? lower(std::string(globalFileName(*rhs.globalRecord)))
                                                   : std::string{};
    if (lhsName != rhsName) {
        return lhsName < rhsName;
    }
    const std::uint32_t lhsModified = lhs.globalRecord ? lhs.globalRecord->modifiedTime : 0;
    const std::uint32_t rhsModified = rhs.globalRecord ? rhs.globalRecord->modifiedTime : 0;
    if (lhsModified != rhsModified) {
        return lhsModified > rhsModified;
    }
    const std::string lhsCategory = lhs.category ? lower(lhs.category->name) : std::string{};
    const std::string rhsCategory = rhs.category ? lower(rhs.category->name) : std::string{};
    return lhsCategory < rhsCategory;
}

void pruneResults(std::vector<SearchResult>& results, size_t keep = kMaxSearchResults)
{
    if (results.size() <= keep) {
        return;
    }
    std::nth_element(results.begin(), results.begin() + static_cast<std::ptrdiff_t>(keep), results.end(), betterResult);
    results.resize(keep);
}

void pushResult(std::vector<SearchResult>& results, SearchResult result)
{
    results.push_back(std::move(result));
    if (results.size() > kMaxSearchResults * 2) {
        pruneResults(results);
    }
}

void collectResults(const std::shared_ptr<const SearchSnapshot>& owner, const Category& category, const std::vector<LaunchItem>& items,
                    const std::string& needle, const AppSettings& settings, int scoreBias, bool globalFile,
                    std::vector<SearchResult>& results, const std::atomic_bool* cancelled)
{
    for (const LaunchItem& item : items) {
        if (searchCancelled(cancelled)) {
            return;
        }
        const int score = scoreItem(item, needle, settings);
        if (score > 0 && item.type != LaunchItemType::Title && item.type != LaunchItemType::Placeholder) {
            pushResult(results, {&category, &item, nullptr, nullptr, score + scoreBias, globalFile, owner});
        }
        if (!item.children.empty()) {
            collectResults(owner, category, item.children, needle, settings, scoreBias, globalFile, results, cancelled);
            if (searchCancelled(cancelled)) {
                return;
            }
        }
    }
}

void collectGlobalResults(const std::shared_ptr<const SearchSnapshot>& owner, const std::string& needle, const AppSettings& settings,
                          const GlobalDirectoryScope& directoryScope, std::vector<SearchResult>& results, const std::atomic_bool* cancelled)
{
    const std::vector<std::string> queryTerms = termsFrom(needle);
    const std::vector<std::string> orderedQueryTerms = orderedTermsFrom(needle);
    const std::vector<std::string> advancedTerms = settings.advancedSearch ? queryTerms : std::vector<std::string>{};
    const GlobalPathQuery pathQuery = parseGlobalPathQuery(needle, orderedQueryTerms);
    std::unordered_set<const IndexedGlobalFileRecord*> emittedGlobalRecords;
    const auto scoreFile = [&](const IndexedGlobalFileRecord& item, bool pathMode) {
        if (!isInGlobalDirectoryScope(item, directoryScope)) {
            return;
        }
        const int baseScore = needle.empty() && directoryScope.enabled
                                  ? 80
                                  : scoreGlobalFileBase(item, needle, advancedTerms, settings, pathMode, &pathQuery);
        if (baseScore <= 0) {
            return;
        }
        if (settings.globalSearchHideSystemPaths && isNoisyGlobalFile(item)) {
            return;
        }
        if (!emittedGlobalRecords.insert(&item).second) {
            return;
        }
        pushResult(results, {&owner->globalCategory, nullptr, nullptr, &item, baseScore + globalFileScoreAdjustment(item), true, owner});
    };

    for (const std::shared_ptr<const GlobalFileChunk>& chunk : owner->globalChunks) {
        if (searchCancelled(cancelled)) {
            return;
        }
        if (!chunk) {
            continue;
        }
        if (pathQuery.enabled || directoryScope.enabled) {
            const std::vector<IndexedGlobalFileRecord>& files = chunk->globalFiles;
            std::string bucketText = pathQuery.enabled ? pathQuery.bucketText : std::string{};
            bucketText += directoryScope.bucketText;
            const int pathBucket = bestBucketIndexForQuery(chunk->pathBuckets, bucketText);
            if (pathBucket >= 0) {
                const std::vector<std::uint32_t>& indexes = chunk->pathBuckets[static_cast<size_t>(pathBucket)];
                for (std::uint32_t index : indexes) {
                    if (searchCancelled(cancelled)) {
                        return;
                    }
                    if (static_cast<size_t>(index) < files.size()) {
                        scoreFile(files[index], pathQuery.enabled);
                    }
                }
            } else {
                for (const IndexedGlobalFileRecord& item : files) {
                    if (searchCancelled(cancelled)) {
                        return;
                    }
                    scoreFile(item, pathQuery.enabled);
                }
            }
            continue;
        }
        const size_t before = results.size();
        const int bucket = bestBucketIndexForQuery(chunk->nameBuckets, needle);
        const std::vector<IndexedGlobalFileRecord>& files = chunk->globalFiles;
        const std::vector<std::uint32_t>* bucketIndexes = nullptr;
        if (bucket >= 0) {
            const std::vector<std::uint32_t>& indexes = chunk->nameBuckets[static_cast<size_t>(bucket)];
            bucketIndexes = &indexes;
            for (std::uint32_t index : indexes) {
                if (searchCancelled(cancelled)) {
                    return;
                }
                if (static_cast<size_t>(index) < files.size()) {
                    scoreFile(files[index], false);
                }
            }
        } else {
            for (const IndexedGlobalFileRecord& item : files) {
                if (searchCancelled(cancelled)) {
                    return;
                }
                scoreFile(item, false);
            }
        }

        if (bucket >= 0 && results.size() - before < kMinGlobalNameResultsBeforeFullScan) {
            std::unordered_set<std::uint32_t> seenBucketIndexes;
            if (bucketIndexes) {
                seenBucketIndexes.reserve(bucketIndexes->size());
                seenBucketIndexes.insert(bucketIndexes->begin(), bucketIndexes->end());
            }
            for (size_t index = 0; index < files.size(); ++index) {
                if (searchCancelled(cancelled)) {
                    return;
                }
                if (seenBucketIndexes.contains(static_cast<std::uint32_t>(index))) {
                    continue;
                }
                scoreFile(files[index], false);
            }
        }
    }
}

struct ParsedSearchQuery {
    std::string needle;
    GlobalDirectoryScope directoryScope;
    bool launcherOnly = false;
    bool notesOnly = false;
    bool globalOnly = false;
};

std::string trimAscii(std::string value)
{
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
                          return std::isspace(ch);
                      }).base();
    if (first >= last) {
        return {};
    }
    return std::string(first, last);
}

std::pair<std::string, std::string> splitDirectoryScopedQuery(std::string_view text)
{
    size_t cursor = 0;
    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
    }
    if (cursor >= text.size()) {
        return {};
    }

    std::string directory;
    if (text[cursor] == '"' || text[cursor] == '\'') {
        const char quote = text[cursor++];
        while (cursor < text.size()) {
            const char ch = text[cursor++];
            if (ch == quote) {
                break;
            }
            directory.push_back(ch);
        }
    } else {
        while (cursor < text.size() && !std::isspace(static_cast<unsigned char>(text[cursor]))) {
            directory.push_back(text[cursor++]);
        }
    }

    while (cursor < text.size() && std::isspace(static_cast<unsigned char>(text[cursor]))) {
        ++cursor;
    }
    return {trimAscii(std::move(directory)), trimAscii(std::string(text.substr(cursor)))};
}

GlobalDirectoryScope makeDirectoryScope(std::string directory)
{
    GlobalDirectoryScope scope;
    directory = trimAscii(std::move(directory));
    if (directory.empty()) {
        return scope;
    }

    scope.enabled = true;
    scope.normalizedRoot = normalizedPathText(directory);
    scope.bucketText = directory;
    return scope;
}

ParsedSearchQuery parseSearchQuery(std::string text)
{
    text = lower(std::move(text));
    if (text.starts_with("qs ")) {
        text.erase(0, 3);
        return {std::move(text), {}, true, false, false};
    }
    if (text.starts_with("note ")) {
        text.erase(0, 5);
        return {std::move(text), {}, false, true, false};
    }
    if (text.starts_with("dir ")) {
        text.erase(0, 4);
        auto [directory, needle] = splitDirectoryScopedQuery(text);
        return {std::move(needle), makeDirectoryScope(std::move(directory)), false, false, true};
    }
    return {std::move(text), {}, false, false, false};
}

} // namespace

void SearchIndex::rebuild(const std::vector<Category>& categories, const std::vector<Note>& notes)
{
    auto next = std::make_shared<SearchSnapshot>();
    next->categories = categories;
    next->notes = notes;
    next->globalCategory.id = "global-files";
    next->globalCategory.name = "Global Search";
    {
        std::lock_guard lock(snapshotMutex_);
        if (snapshot_) {
            next->globalChunks = snapshot_->globalChunks;
            next->globalFileCount = snapshot_->globalFileCount;
        }
        snapshot_ = std::move(next);
        ++revision_;
    }
}

void SearchIndex::rebuildGlobalFiles(std::vector<GlobalFileRecord> files)
{
    auto next = std::make_shared<SearchSnapshot>();
    next->globalCategory.id = "global-files";
    next->globalCategory.name = "Global Search";
    if (!files.empty()) {
        next->globalFileCount = files.size();
        next->globalChunks.push_back(buildGlobalChunk(std::move(files), "default"));
    }
    {
        std::lock_guard lock(snapshotMutex_);
        if (snapshot_) {
            next->categories = snapshot_->categories;
            next->notes = snapshot_->notes;
        }
        snapshot_ = std::move(next);
        ++revision_;
    }
}

void SearchIndex::appendGlobalFiles(std::vector<GlobalFileRecord> files)
{
    appendGlobalRoot("default", std::move(files));
}

void SearchIndex::replaceGlobalRoot(std::string rootId, std::vector<GlobalFileRecord> files)
{
    if (rootId.empty()) {
        rootId = "default";
    }

    std::shared_ptr<const GlobalFileChunk> chunk;
    const size_t addedCount = files.size();
    if (!files.empty()) {
        chunk = buildGlobalChunk(std::move(files), rootId);
    }

    auto next = std::make_shared<SearchSnapshot>();
    next->globalCategory.id = "global-files";
    next->globalCategory.name = "Global Search";
    {
        std::lock_guard lock(snapshotMutex_);
        if (snapshot_) {
            next->categories = snapshot_->categories;
            next->notes = snapshot_->notes;
            next->globalChunks.reserve(snapshot_->globalChunks.size() + 1);
            for (const std::shared_ptr<const GlobalFileChunk>& existing : snapshot_->globalChunks) {
                if (!existing || existing->rootId == rootId) {
                    continue;
                }
                next->globalChunks.push_back(existing);
                next->globalFileCount += existing->globalFiles.size();
            }
        }
        if (chunk) {
            next->globalFileCount += addedCount;
            next->globalChunks.push_back(std::move(chunk));
        }
        snapshot_ = std::move(next);
        ++revision_;
    }
}

void SearchIndex::appendGlobalRoot(std::string rootId, std::vector<GlobalFileRecord> files)
{
    if (files.empty()) {
        return;
    }
    if (rootId.empty()) {
        rootId = "default";
    }

    const size_t addedCount = files.size();
    std::shared_ptr<const GlobalFileChunk> chunk = buildGlobalChunk(std::move(files), rootId);
    auto next = std::make_shared<SearchSnapshot>();
    next->globalCategory.id = "global-files";
    next->globalCategory.name = "Global Search";
    {
        std::lock_guard lock(snapshotMutex_);
        if (snapshot_) {
            next->categories = snapshot_->categories;
            next->notes = snapshot_->notes;
            next->globalChunks = snapshot_->globalChunks;
            next->globalFileCount = snapshot_->globalFileCount;
        }
        next->globalFileCount += addedCount;
        next->globalChunks.push_back(std::move(chunk));
        snapshot_ = std::move(next);
        ++revision_;
    }
}

std::shared_ptr<const SearchSnapshot> SearchIndex::snapshot() const
{
    std::lock_guard lock(snapshotMutex_);
    return snapshot_;
}

std::uint64_t SearchIndex::revision() const
{
    std::lock_guard lock(snapshotMutex_);
    return revision_;
}

std::vector<SearchResult> SearchIndex::query(const std::string& text, const AppSettings& settings) const
{
    return query(text, settings, nullptr);
}

std::vector<SearchResult> SearchIndex::query(const std::string& text, const AppSettings& settings, const std::atomic_bool* cancelled) const
{
    std::vector<SearchResult> results;
    const std::shared_ptr<const SearchSnapshot> current = snapshot();
    if (!current || (current->categories.empty() && current->notes.empty() && current->globalFileCount == 0)) {
        return results;
    }

    const ParsedSearchQuery parsed = parseSearchQuery(text);
    const std::string& needle = parsed.needle;
    if (!parsed.notesOnly && !parsed.globalOnly) {
        for (const Category& category : current->categories) {
            if (searchCancelled(cancelled)) {
                return {};
            }
            collectResults(current, category, category.items, needle, settings,
                           settings.enableGlobalSearch && !parsed.launcherOnly ? kInternalItemScoreBias : 0, false, results, cancelled);
        }
    }
    if (!parsed.launcherOnly && !parsed.globalOnly) {
        for (const Note& note : current->notes) {
            if (searchCancelled(cancelled)) {
                return {};
            }
            const int score = scoreNote(note, needle, settings);
            if (score > 0) {
                pushResult(results, {nullptr, nullptr, &note, nullptr, score + 160, false, current});
            }
        }
    }
    const bool directoryScopedGlobalSearch = parsed.directoryScope.enabled;
    if (!parsed.launcherOnly && !parsed.notesOnly && settings.enableGlobalSearch &&
        (directoryScopedGlobalSearch || shouldSearchGlobalFiles(needle)) && current->globalFileCount > 0) {
        collectGlobalResults(current, needle, settings, parsed.directoryScope, results, cancelled);
        if (searchCancelled(cancelled)) {
            return {};
        }
    }

    if (results.size() > kMaxSearchResults) {
        pruneResults(results, kMaxSearchResults);
    }
    std::sort(results.begin(), results.end(), betterResult);
    return results;
}

} // namespace launcher
