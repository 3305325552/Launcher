#include "core/LaunchParameterUtils.hpp"

#include "core/StringEncoding.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace launcher::launch_params {
namespace {

constexpr size_t kMaxHistoryEntries = 32;
constexpr size_t kMaxHistorySuggestions = 8;

std::string replaceAll(std::string value, const std::string& from, const std::string& to)
{
    if (from.empty()) {
        return value;
    }
    std::size_t pos = 0;
    while ((pos = value.find(from, pos)) != std::string::npos) {
        value.replace(pos, from.size(), to);
        pos += to.size();
    }
    return value;
}

std::string urlEncode(const std::string& value)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (unsigned char ch : value) {
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
            ch == '~') {
            encoded.push_back(static_cast<char>(ch));
        } else if (ch == ' ') {
            encoded.push_back('+');
        } else {
            encoded.push_back('%');
            encoded.push_back(hex[ch >> 4]);
            encoded.push_back(hex[ch & 0x0f]);
        }
    }
    return encoded;
}

std::string asciiLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool startsWithIgnoreCase(const std::string& value, const std::string& prefix)
{
    return !prefix.empty() && value.size() >= prefix.size() && asciiLower(value.substr(0, prefix.size())) == asciiLower(prefix);
}

void pruneInteractiveHistory(InteractiveParam& param)
{
    param.history.erase(std::remove_if(param.history.begin(), param.history.end(),
                                       [](const InteractiveParamHistory& history) {
                                           return history.value.empty();
                                       }),
                        param.history.end());
    std::stable_sort(param.history.begin(), param.history.end(), [](const InteractiveParamHistory& a, const InteractiveParamHistory& b) {
        if (a.useCount != b.useCount) return a.useCount > b.useCount;
        if (a.lastUsedAt != b.lastUsedAt) return a.lastUsedAt > b.lastUsedAt;
        return a.value < b.value;
    });
    if (param.history.size() > kMaxHistoryEntries) {
        param.history.resize(kMaxHistoryEntries);
    }
}

std::string replaceInteractiveValue(std::string value, const std::string& id, const std::string& replacement)
{
    value = replaceAll(value, "{{" + id + "}}", replacement);
    value = replaceAll(value, "%" + id + "%", replacement);
    return value;
}

} // namespace

LaunchItem withSearchVariables(const LaunchItem& item, const std::string& searchText)
{
    LaunchItem result = item;
    const std::string encoded = urlEncode(searchText);
    result.target = pathFromUtf8(replaceAll(replaceAll(pathToUtf8(result.target), "%so%", searchText), "%so-url%", encoded));
    result.startDirectory =
        pathFromUtf8(replaceAll(replaceAll(pathToUtf8(result.startDirectory), "%so%", searchText), "%so-url%", encoded));
    result.arguments = replaceAll(replaceAll(result.arguments, "%so%", searchText), "%so-url%", encoded);
    result.icon = replaceAll(replaceAll(result.icon, "%so%", searchText), "%so-url%", encoded);
    return result;
}

std::string effectiveParamId(const InteractiveParam& param, int index)
{
    return param.id.empty() ? "param" + std::to_string(index + 1) : param.id;
}

std::string defaultParamValue(const InteractiveParam& param)
{
    if (param.kind == InteractiveParamKind::Choice) {
        if (!param.defaultValue.empty()) {
            return param.defaultValue;
        }
        return param.choices.empty() ? std::string{} : param.choices.front();
    }
    if (param.kind == InteractiveParamKind::Number) {
        double value = param.defaultValue.empty() ? param.minValue : std::strtod(param.defaultValue.c_str(), nullptr);
        if (param.maxValue >= param.minValue) {
            value = std::clamp(value, param.minValue, param.maxValue);
        }
        char buffer[64]{};
        std::snprintf(buffer, sizeof(buffer), "%.6g", value);
        return buffer;
    }
    return param.defaultValue;
}

std::string interactiveParamKey(const std::string& itemId, const InteractiveParam& param, int index)
{
    return itemId + ":" + effectiveParamId(param, index);
}

std::vector<HistoryCandidate> interactiveHistoryCandidates(const InteractiveParam& param, const std::string& input)
{
    std::vector<HistoryCandidate> candidates;
    candidates.reserve(param.history.size());
    for (int i = 0; i < static_cast<int>(param.history.size()); ++i) {
        const InteractiveParamHistory& history = param.history[static_cast<size_t>(i)];
        if (history.value.empty()) {
            continue;
        }
        candidates.push_back(
            HistoryCandidate{history.value, history.useCount, history.lastUsedAt, i, startsWithIgnoreCase(history.value, input)});
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const HistoryCandidate& a, const HistoryCandidate& b) {
        if (a.useCount != b.useCount) return a.useCount > b.useCount;
        if (a.lastUsedAt != b.lastUsedAt) return a.lastUsedAt > b.lastUsedAt;
        return a.value < b.value;
    });
    if (candidates.size() > kMaxHistorySuggestions) {
        candidates.resize(kMaxHistorySuggestions);
    }
    return candidates;
}

void recordInteractiveHistory(LaunchItem& item, const std::vector<std::string>& values, std::int64_t timestamp)
{
    for (int i = 0; i < static_cast<int>(item.interactiveParams.size()) && i < static_cast<int>(values.size()); ++i) {
        const std::string& value = values[static_cast<size_t>(i)];
        if (value.empty()) {
            continue;
        }
        InteractiveParam& param = item.interactiveParams[static_cast<size_t>(i)];
        auto it = std::find_if(param.history.begin(), param.history.end(), [&](const InteractiveParamHistory& history) {
            return history.value == value;
        });
        if (it == param.history.end()) {
            param.history.push_back(InteractiveParamHistory{value, 1, timestamp});
        } else {
            it->useCount = std::max(0, it->useCount) + 1;
            it->lastUsedAt = timestamp;
        }
        pruneInteractiveHistory(param);
    }
}

void removeInteractiveHistoryValue(LaunchItem& item, int paramIndex, const std::string& value)
{
    if (paramIndex < 0 || paramIndex >= static_cast<int>(item.interactiveParams.size())) {
        return;
    }
    InteractiveParam& param = item.interactiveParams[static_cast<size_t>(paramIndex)];
    param.history.erase(std::remove_if(param.history.begin(), param.history.end(),
                                       [&](const InteractiveParamHistory& history) {
                                           return history.value == value;
                                       }),
                        param.history.end());
}

bool itemNeedsInteractivePrompt(const LaunchItem& item)
{
    return item.interactive && !item.interactiveParams.empty() && item.type != LaunchItemType::VirtualFolder &&
           item.type != LaunchItemType::Title && item.type != LaunchItemType::Placeholder && item.type != LaunchItemType::Note;
}

LaunchItem withInteractiveValues(const LaunchItem& item, const std::vector<std::string>& values)
{
    LaunchItem result = item;
    for (int i = 0; i < static_cast<int>(item.interactiveParams.size()); ++i) {
        const std::string id = effectiveParamId(item.interactiveParams[static_cast<size_t>(i)], i);
        const std::string value = i < static_cast<int>(values.size()) ? values[static_cast<size_t>(i)]
                                                                      : defaultParamValue(item.interactiveParams[static_cast<size_t>(i)]);
        result.target = pathFromUtf8(replaceInteractiveValue(pathToUtf8(result.target), id, value));
        result.startDirectory = pathFromUtf8(replaceInteractiveValue(pathToUtf8(result.startDirectory), id, value));
        result.arguments = replaceInteractiveValue(result.arguments, id, value);
        result.icon = replaceInteractiveValue(result.icon, id, value);
    }
    return result;
}

} // namespace launcher::launch_params
