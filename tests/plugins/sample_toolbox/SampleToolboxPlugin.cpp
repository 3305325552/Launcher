#include <windows.h>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
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

std::string trim(std::string value)
{
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
                         return std::isspace(ch) != 0;
                     }).base();
    return begin < end ? std::string(begin, end) : std::string{};
}

std::string setting(const json& params, const char* key, const char* fallback = "")
{
    if (!params.contains("settings") || !params["settings"].is_object()) {
        return fallback;
    }
    return params["settings"].value(key, fallback);
}

bool truthy(const std::string& value)
{
    const std::string normalized = lower(value);
    return normalized == "1" || normalized == "true" || normalized == "yes" || normalized == "on";
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
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(static_cast<size_t>(std::max(length, 0)), L'\0');
    if (length > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), length);
    }
    return result;
}

void copyText(const std::string& text)
{
    const std::wstring wide = wideFromUtf8(text);
    const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory) {
        return;
    }
    if (void* locked = GlobalLock(memory)) {
        std::memcpy(locked, wide.c_str(), bytes);
        GlobalUnlock(memory);
    }
    if (!OpenClipboard(nullptr)) {
        GlobalFree(memory);
        return;
    }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, memory);
    CloseClipboard();
}

std::string pluginFolderName()
{
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    return lower(std::filesystem::path(path).parent_path().filename().string());
}

std::optional<double> parseNumber(const std::string& text, size_t& pos)
{
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos])) != 0) {
        ++pos;
    }
    const char* begin = text.data() + pos;
    char* end = nullptr;
    const double value = std::strtod(begin, &end);
    if (end == begin) {
        return std::nullopt;
    }
    pos = static_cast<size_t>(end - text.data());
    return value;
}

std::optional<double> evalBinaryExpression(const std::string& expression)
{
    size_t pos = 0;
    const std::optional<double> lhs = parseNumber(expression, pos);
    if (!lhs) {
        return std::nullopt;
    }
    while (pos < expression.size() && std::isspace(static_cast<unsigned char>(expression[pos])) != 0) {
        ++pos;
    }
    if (pos >= expression.size()) {
        return lhs;
    }
    const char op = expression[pos++];
    const std::optional<double> rhs = parseNumber(expression, pos);
    if (!rhs) {
        return std::nullopt;
    }
    switch (op) {
    case '+': return *lhs + *rhs;
    case '-': return *lhs - *rhs;
    case '*': return *lhs * *rhs;
    case '/': return std::abs(*rhs) < 0.0000001 ? std::nullopt : std::optional<double>(*lhs / *rhs);
    default: return std::nullopt;
    }
}

std::string formatDouble(double value, int precision)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(std::clamp(precision, 0, 8)) << value;
    std::string text = out.str();
    if (text.find('.') != std::string::npos) {
        while (!text.empty() && text.back() == '0') {
            text.pop_back();
        }
        if (!text.empty() && text.back() == '.') {
            text.pop_back();
        }
    }
    return text.empty() ? "0" : text;
}

json searchCalc(const json& params)
{
    const std::string query = params.value("query", "");
    const std::string normalized = lower(query);
    if (!normalized.starts_with("calc ")) {
        return json::array();
    }
    const std::string expression = trim(query.substr(5));
    const std::optional<double> value = evalBinaryExpression(expression);
    if (!value) {
        return json::array({{{"id", "calc:error:" + expression},
                             {"title", "Calc: invalid expression"},
                             {"subtitle", "Use: calc 12.5*4"},
                             {"icon", "calculate"},
                             {"score", 210},
                             {"actions", json::array({{{"id", "open"}, {"label", "Write Log"}}})}}});
    }
    int precision = 2;
    try {
        precision = static_cast<int>(std::stod(setting(params, "precision", "2")));
    } catch (...) {}
    const std::string result = formatDouble(*value, precision);
    return json::array(
        {{{"id", "calc:" + expression + "=" + result},
          {"title", "Calc: " + expression + " = " + result},
          {"subtitle", "Simple sample calculator plugin"},
          {"icon", "calculate"},
          {"score", 260},
          {"actions", json::array({{{"id", "open"}, {"label", "Write Log"}}, {{"id", "copy-result"}, {"label", "Copy Result"}}})}}});
}

bool parseHexColor(std::string text, int& r, int& g, int& b)
{
    text = trim(text);
    if (lower(text).starts_with("color ")) {
        text = trim(text.substr(6));
    }
    if (!text.empty() && text[0] == '#') {
        text.erase(text.begin());
    }
    if (text.size() != 6 || !std::all_of(text.begin(), text.end(), [](unsigned char ch) {
            return std::isxdigit(ch) != 0;
        })) {
        return false;
    }
    r = std::stoi(text.substr(0, 2), nullptr, 16);
    g = std::stoi(text.substr(2, 2), nullptr, 16);
    b = std::stoi(text.substr(4, 2), nullptr, 16);
    return true;
}

json searchColor(const json& params)
{
    const std::string query = params.value("query", "");
    int r = 0;
    int g = 0;
    int b = 0;
    if (!parseHexColor(query, r, g, b)) {
        return json::array();
    }
    char hex[16]{};
    std::snprintf(hex, sizeof(hex), "#%02X%02X%02X", r, g, b);
    std::string hexText = hex;
    if (!truthy(setting(params, "uppercaseHex", "true"))) {
        hexText = lower(hexText);
    }
    const std::string rgbText = "rgb(" + std::to_string(r) + ", " + std::to_string(g) + ", " + std::to_string(b) + ")";
    const std::string format = setting(params, "format", "rgb");
    const std::string subtitle = format == "hex" ? hexText : format == "both" ? hexText + " / " + rgbText : rgbText;
    return json::array({{{"id", "color:" + hexText},
                         {"title", "Color " + hexText},
                         {"subtitle", subtitle},
                         {"icon", "palette"},
                         {"score", 250},
                         {"actions", json::array({{{"id", "open"}, {"label", "Write Log"}},
                                                  {{"id", "copy-hex"}, {"label", "Copy Hex"}},
                                                  {{"id", "copy-rgb"}, {"label", "Copy RGB"}}})}}});
}

json searchCli(const json& params)
{
    const std::string query = params.value("query", "");
    const std::string normalized = lower(query);
    if (!normalized.starts_with("cli ")) {
        return json::array();
    }
    const std::string argument = trim(query.substr(4));
    const std::string tool = setting(params, "tool", "echo");
    const bool dryRun = truthy(setting(params, "dryRun", "true"));
    const std::string command = tool + " " + argument;
    return json::array(
        {{{"id", "cli:" + command},
          {"title", "CLI Preview: " + command},
          {"subtitle", std::string(dryRun ? "Dry run" : "Runnable") + " in " + setting(params, "workingDirectory", ".")},
          {"icon", "terminal"},
          {"score", 240},
          {"actions", json::array({{{"id", "open"}, {"label", "Write Log"}}, {{"id", "copy-command"}, {"label", "Copy Command"}}})}}});
}

json search(const json& params)
{
    const std::string folder = pluginFolderName();
    if (folder.find("calc") != std::string::npos) {
        return searchCalc(params);
    }
    if (folder.find("color") != std::string::npos) {
        return searchColor(params);
    }
    if (folder.find("cli") != std::string::npos) {
        return searchCli(params);
    }
    return json::array();
}

json run(const json& params)
{
    const std::string resultId = params.value("resultId", "");
    const std::string actionId = params.value("actionId", "open");
    appendLog(cacheDirectory(params), "runs.log", "action=" + actionId + " result=" + resultId);
    if (actionId == "copy-result") {
        const size_t eq = resultId.find('=');
        copyText(eq == std::string::npos ? resultId : resultId.substr(eq + 1));
    } else if (actionId == "copy-hex") {
        copyText(resultId.rfind("color:", 0) == 0 ? resultId.substr(6) : resultId);
    } else if (actionId == "copy-rgb") {
        int r = 0;
        int g = 0;
        int b = 0;
        if (parseHexColor(resultId.rfind("color:", 0) == 0 ? resultId.substr(6) : resultId, r, g, b)) {
            copyText("rgb(" + std::to_string(r) + ", " + std::to_string(g) + ", " + std::to_string(b) + ")");
        }
    } else if (actionId == "copy-command") {
        copyText(resultId.rfind("cli:", 0) == 0 ? resultId.substr(4) : resultId);
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
            result = json{{"error", "unknown method"}};
        }
        std::cout << json{{"id", request.value("id", 0)}, {"result", result}}.dump() << "\n";
    } catch (const std::exception& ex) {
        std::cout << json{{"error", ex.what()}}.dump() << "\n";
    }
    return 0;
}
