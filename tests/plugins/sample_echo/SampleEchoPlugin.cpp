#include <windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using json = nlohmann::json;

namespace {

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool truthy(const std::string& value)
{
    const std::string normalized = lower(value);
    return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
}

std::string setting(const json& params, const char* key, const char* fallback = "")
{
    const json* settings = params.find("settings") == params.end() ? nullptr : &params["settings"];
    if (!settings || !settings->is_object()) {
        return fallback;
    }
    return settings->value(key, fallback);
}

int scoreBoost(const json& params)
{
    try {
        return std::clamp(static_cast<int>(std::stod(setting(params, "scoreBoost", "0"))), -100, 100);
    } catch (...) {
        return 0;
    }
}

std::filesystem::path cacheDirectory(const json& params)
{
    return std::filesystem::path(params.value("cacheDir", ""));
}

std::string timestamp()
{
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    tm local{};
    localtime_s(&local, &now);
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

void appendLog(const std::filesystem::path& directory, const std::string& fileName, const std::string& line)
{
    if (directory.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    std::ofstream output(directory / fileName, std::ios::app);
    output << timestamp() << " " << line << "\n";
}

std::wstring wideFromUtf8(const std::string& text)
{
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(std::max(length, 0)), L'\0');
    if (length > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length);
    }
    return result;
}

bool copyText(const std::string& text)
{
    const std::wstring wide = wideFromUtf8(text);
    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        return false;
    }
    if (void* locked = GlobalLock(memory)) {
        std::memcpy(locked, wide.c_str(), bytes);
        GlobalUnlock(memory);
    }
    if (!OpenClipboard(nullptr)) {
        GlobalFree(memory);
        return false;
    }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
    return true;
}

json search(const json& params)
{
    const std::string query = params.value("query", "");
    const std::string normalized = lower(query);
    if (normalized != "tp" && normalized != "test plugin" && normalized.find("plugin") == std::string::npos &&
        normalized.find("echo") == std::string::npos) {
        return json::array();
    }

    const std::string prefix = setting(params, "prefix", "Echo");
    const std::string mode = setting(params, "mode", "normal");
    const bool includeTimestamp = truthy(setting(params, "includeTimestamp", "false"));
    const int boost = scoreBoost(params);

    std::string suffix = "mode=" + mode;
    if (includeTimestamp) {
        suffix += " time=" + timestamp();
    }

    std::string upper = query;
    std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });

    return json::array({
        {{"id", "echo:" + query},
         {"title", prefix + " " + query},
         {"subtitle", "Sample plugin result, " + suffix},
         {"icon", "extension"},
         {"score", 230 + boost},
         {"actions", json::array({{{"id", "open"}, {"label", "Write Run Log"}},
                                  {{"id", "copy-id"}, {"label", "Copy Result ID"}},
                                  {{"id", "write-cache"}, {"label", "Write Cache Log"}}})}},
        {{"id", "upper:" + query},
         {"title", prefix + " " + upper},
         {"subtitle", "Uppercase variant from Sample Echo Plugin"},
         {"icon", "terminal"},
         {"score", 190 + boost},
         {"actions", json::array({{{"id", "open"}, {"label", "Write Run Log"}}, {{"id", "copy-id"}, {"label", "Copy Result ID"}}})}},
    });
}

json run(const json& params)
{
    const std::string resultId = params.value("resultId", "");
    const std::string actionId = params.value("actionId", "open");
    appendLog(cacheDirectory(params), "runs.log", "action=" + actionId + " result=" + resultId);
    if (actionId == "copy-id") {
        copyText(resultId);
    }
    return json{{"ok", true}};
}

json event(const json& params)
{
    appendLog(cacheDirectory(params), "events.log", "event=" + params.value("name", ""));
    return json{{"ok", true}};
}

} // namespace

int main()
{
    std::string line;
    if (!std::getline(std::cin, line)) {
        return 1;
    }

    try {
        const json request = json::parse(line);
        const std::string method = request.value("method", "");
        const json params = request.value("params", json::object());
        json result;
        if (method == "search") {
            result = search(params);
        } else if (method == "run") {
            result = run(params);
        } else if (method == "event") {
            result = event(params);
        } else {
            std::cout << json{{"error", "unknown method"}}.dump() << "\n";
            return 0;
        }
        std::cout << json{{"id", request.value("id", 0)}, {"result", result}}.dump() << "\n";
    } catch (const std::exception& ex) {
        std::cout << json{{"error", ex.what()}}.dump() << "\n";
    }
    return 0;
}
