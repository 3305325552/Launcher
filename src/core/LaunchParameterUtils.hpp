#pragma once

#include "launcher/Models.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace launcher::launch_params {

struct HistoryCandidate {
    std::string value;
    int useCount = 0;
    std::int64_t lastUsedAt = 0;
    int sourceIndex = -1;
    bool prefixMatch = false;
};

LaunchItem withSearchVariables(const LaunchItem& item, const std::string& searchText);
std::string effectiveParamId(const InteractiveParam& param, int index);
std::string defaultParamValue(const InteractiveParam& param);
std::string interactiveParamKey(const std::string& itemId, const InteractiveParam& param, int index);
std::vector<HistoryCandidate> interactiveHistoryCandidates(const InteractiveParam& param, const std::string& input);
void recordInteractiveHistory(LaunchItem& item, const std::vector<std::string>& values, std::int64_t timestamp);
void removeInteractiveHistoryValue(LaunchItem& item, int paramIndex, const std::string& value);
bool itemNeedsInteractivePrompt(const LaunchItem& item);
LaunchItem withInteractiveValues(const LaunchItem& item, const std::vector<std::string>& values);

} // namespace launcher::launch_params
