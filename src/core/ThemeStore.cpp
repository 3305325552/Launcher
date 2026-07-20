#include "core/ThemeStore.hpp"

#include "core/StringEncoding.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

namespace launcher {
namespace {

using json = nlohmann::json;

constexpr int kThemeSchemaVersion = 1;
constexpr const char* kBuiltinPrefix = "builtin:";
constexpr const char* kCustomPrefix = "custom:";

std::string prefixedId(bool builtin, const std::string& id)
{
    const std::string fallback = builtin ? "theme" : "custom-theme";
    const std::string raw = id.empty() ? fallback : id;
    if (raw.rfind(kBuiltinPrefix, 0) == 0 || raw.rfind(kCustomPrefix, 0) == 0) {
        return raw;
    }
    return std::string(builtin ? kBuiltinPrefix : kCustomPrefix) + raw;
}

std::string unprefixedId(std::string id)
{
    if (id.rfind(kBuiltinPrefix, 0) == 0) {
        return id.substr(std::char_traits<char>::length(kBuiltinPrefix));
    }
    if (id.rfind(kCustomPrefix, 0) == 0) {
        return id.substr(std::char_traits<char>::length(kCustomPrefix));
    }
    return id;
}

void setError(std::string* error, const std::string& message)
{
    if (error) {
        *error = message;
    }
}

float clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

bool parseHexByte(std::string_view text, int offset, float& out)
{
    if (offset + 2 > static_cast<int>(text.size())) {
        return false;
    }
    unsigned int value = 0;
    const std::string part(text.substr(static_cast<size_t>(offset), 2));
    if (std::sscanf(part.c_str(), "%02x", &value) != 1) {
        return false;
    }
    out = std::clamp(static_cast<float>(value) / 255.0f, 0.0f, 1.0f);
    return true;
}

bool readColor(const json& value, ThemeColor& color)
{
    if (value.is_string()) {
        std::string text = value.get<std::string>();
        if (!text.empty() && text.front() == '#') {
            text.erase(text.begin());
        }
        if (text.size() != 6 && text.size() != 8) {
            return false;
        }
        float alpha = 1.0f;
        if (!parseHexByte(text, 0, color.color[0]) || !parseHexByte(text, 2, color.color[1]) || !parseHexByte(text, 4, color.color[2])) {
            return false;
        }
        if (text.size() == 8 && !parseHexByte(text, 6, alpha)) {
            return false;
        }
        color.color[3] = alpha;
        return true;
    }

    if (!value.is_array() || value.empty()) {
        return false;
    }
    for (int i = 0; i < 4; ++i) {
        color.color[i] = i < static_cast<int>(value.size()) ? clamp01(value[static_cast<size_t>(i)].get<float>()) : (i == 3 ? 1.0f : 0.0f);
    }
    return true;
}

std::string colorHex(const ThemeColor& color)
{
    const int r = std::clamp(static_cast<int>(color.color[0] * 255.0f + 0.5f), 0, 255);
    const int g = std::clamp(static_cast<int>(color.color[1] * 255.0f + 0.5f), 0, 255);
    const int b = std::clamp(static_cast<int>(color.color[2] * 255.0f + 0.5f), 0, 255);
    const int a = std::clamp(static_cast<int>(color.color[3] * 255.0f + 0.5f), 0, 255);
    char buffer[10]{};
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X%02X", r, g, b, a);
    return buffer;
}

std::filesystem::path resolveThemePath(const std::filesystem::path& themePath, const std::string& value)
{
    if (value.empty()) {
        return {};
    }
    std::filesystem::path path = pathFromUtf8(value);
    if (path.is_relative()) {
        path = themePath.parent_path() / path;
    }
    return path.lexically_normal();
}

std::string storedThemePath(const std::filesystem::path& themePath, const std::filesystem::path& value)
{
    if (value.empty()) {
        return {};
    }
    std::error_code ec;
    const std::filesystem::path relative = std::filesystem::relative(value, themePath.parent_path(), ec);
    if (!ec && !relative.empty() && relative.native().find(L"..") != 0) {
        return narrow(relative.generic_wstring());
    }
    return pathToUtf8(value);
}

std::string sanitizeFileStem(std::string value)
{
    if (value.empty()) {
        value = "custom-theme";
    }
    for (char& ch : value) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && ch != '-' && ch != '_') {
            ch = '-';
        }
    }
    value.erase(std::unique(value.begin(), value.end(),
                            [](char lhs, char rhs) {
                                return lhs == '-' && rhs == '-';
                            }),
                value.end());
    while (!value.empty() && value.front() == '-') {
        value.erase(value.begin());
    }
    while (!value.empty() && value.back() == '-') {
        value.pop_back();
    }
    return value.empty() ? "custom-theme" : value;
}

std::string sanitizeFileName(std::string value)
{
    if (value.empty()) {
        value = "background.bin";
    }
    for (char& ch : value) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (!std::isalnum(c) && ch != '-' && ch != '_' && ch != '.') {
            ch = '-';
        }
    }
    return value.empty() ? "background.bin" : value;
}

std::string mimeForPath(const std::filesystem::path& path)
{
    std::string ext = pathToUtf8(path.extension());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".bmp") return "image/bmp";
    if (ext == ".webp") return "image/webp";
    return "image/png";
}

const char* base64Alphabet()
{
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

std::string base64Encode(const std::vector<unsigned char>& bytes)
{
    std::string result;
    result.reserve(((bytes.size() + 2) / 3) * 4);
    const char* alphabet = base64Alphabet();
    for (size_t i = 0; i < bytes.size(); i += 3) {
        const unsigned int b0 = bytes[i];
        const unsigned int b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0;
        const unsigned int b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0;
        result.push_back(alphabet[(b0 >> 2) & 0x3f]);
        result.push_back(alphabet[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0f)]);
        result.push_back(i + 1 < bytes.size() ? alphabet[((b1 & 0x0f) << 2) | ((b2 >> 6) & 0x03)] : '=');
        result.push_back(i + 2 < bytes.size() ? alphabet[b2 & 0x3f] : '=');
    }
    return result;
}

bool base64Decode(const std::string& text, std::vector<unsigned char>& bytes)
{
    int decode[256];
    std::fill(std::begin(decode), std::end(decode), -1);
    const char* alphabet = base64Alphabet();
    for (int i = 0; alphabet[i] != '\0'; ++i) {
        decode[static_cast<unsigned char>(alphabet[i])] = i;
    }

    bytes.clear();
    int value = 0;
    int bits = -8;
    for (unsigned char ch : text) {
        if (std::isspace(ch)) {
            continue;
        }
        if (ch == '=') {
            break;
        }
        if (decode[ch] < 0) {
            return false;
        }
        value = (value << 6) + decode[ch];
        bits += 6;
        if (bits >= 0) {
            bytes.push_back(static_cast<unsigned char>((value >> bits) & 0xff));
            bits -= 8;
        }
    }
    return true;
}

std::vector<unsigned char> readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return {};
    }
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

void hydrateEmbeddedBackground(const std::filesystem::path& themePath, ThemeDefinition& theme)
{
    if (theme.background.imageEmbeddedBase64.empty()) {
        return;
    }
    std::error_code ec;
    if (!theme.background.imagePath.empty() && std::filesystem::exists(theme.background.imagePath, ec)) {
        return;
    }

    std::vector<unsigned char> bytes;
    if (!base64Decode(theme.background.imageEmbeddedBase64, bytes) || bytes.empty()) {
        return;
    }

    const std::string fileName =
        sanitizeFileName(theme.background.imageEmbeddedName.empty() ? "background.png" : theme.background.imageEmbeddedName);
    const std::filesystem::path cacheDir = themePath.parent_path() / ".images";
    std::filesystem::create_directories(cacheDir, ec);
    if (ec) {
        return;
    }
    const std::filesystem::path cachePath = cacheDir / (sanitizeFileStem(unprefixedId(theme.id)) + "-" + fileName);
    std::ofstream output(cachePath, std::ios::binary);
    if (!output) {
        return;
    }
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (output) {
        theme.background.imagePath = cachePath;
    }
}

void embedBackgroundIfNeeded(ThemeDefinition& theme)
{
    if (theme.background.imagePath.empty()) {
        theme.background.imageEmbeddedBase64.clear();
        return;
    }
    if (theme.background.animated) {
        theme.background.imageEmbeddedBase64.clear();
        theme.background.imageEmbeddedName.clear();
        theme.background.imageEmbeddedMime.clear();
        return;
    }
    std::error_code ec;
    if (!std::filesystem::exists(theme.background.imagePath, ec)) {
        return;
    }
    const std::vector<unsigned char> bytes = readBinaryFile(theme.background.imagePath);
    if (bytes.empty()) {
        return;
    }
    theme.background.imageEmbeddedName = sanitizeFileName(pathToUtf8(theme.background.imagePath.filename()));
    theme.background.imageEmbeddedMime = mimeForPath(theme.background.imagePath);
    theme.background.imageEmbeddedBase64 = base64Encode(bytes);
}

void loadDirectory(const std::filesystem::path& directory, bool builtin, std::vector<ThemeDefinition>& themes,
                   std::vector<ThemeCatalogEntry>& entries)
{
    std::error_code ec;
    if (!std::filesystem::exists(directory, ec)) {
        return;
    }

    std::vector<std::filesystem::path> files;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".json") {
            continue;
        }
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());

    for (const std::filesystem::path& path : files) {
        ThemeDefinition theme;
        if (!loadThemeFile(path, builtin, theme)) {
            continue;
        }
        entries.push_back({theme.id, theme.name, theme.author, path, builtin});
        themes.push_back(std::move(theme));
    }
}

} // namespace

ThemeDefinition fallbackTheme(bool dark)
{
    ThemeDefinition theme;
    theme.id = dark ? "builtin:dark" : "builtin:light";
    theme.name = dark ? "Dark" : "Light";
    theme.author = "Launcher";
    theme.dark = dark;
    theme.builtin = true;
    return theme;
}

bool loadThemeFile(const std::filesystem::path& path, bool builtin, ThemeDefinition& theme, std::string* error)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        setError(error, "failed to open theme file");
        return false;
    }

    try {
        json document;
        input >> document;
        const std::string rawId = document.value("id", pathToUtf8(path.stem()));
        theme = {};
        theme.id = prefixedId(builtin, rawId);
        theme.name = document.value("name", rawId.empty() ? pathToUtf8(path.stem()) : rawId);
        theme.author = document.value("author", "Launcher");
        theme.dark = document.value("dark", false);
        theme.sourcePath = path;
        theme.builtin = builtin;

        const json window = document.value("window", json::object());
        theme.windowTransparent = window.value("transparent", false);
        theme.windowOpacity = std::clamp(window.value("opacity", 100), 1, 100);
        theme.windowTransparencyForAll = window.value("transparentForAll", false);
        theme.secondaryWindowOpacity = std::clamp(window.value("secondaryOpacity", 100), 1, 100);

        const json popup = document.value("popup", json::object());
        theme.popupMenuOpacity = std::clamp(popup.value("opacity", 100), 0, 100);

        const json background = document.value("background", json::object());
        theme.background.enabled = background.value("enabled", false);
        theme.background.imagePath = resolveThemePath(path, background.value("image", ""));
        theme.background.imageMode = std::clamp(background.value("mode", 0), 0, 4);
        theme.background.opacity = std::clamp(background.value("opacity", 100), 0, 100);
        theme.background.animated = background.value("animated", false);
        theme.background.animationFps = std::clamp(background.value("fps", 15), 1, 30);
        theme.background.animationMaxWidth = std::clamp(background.value("maxWidth", 960), 240, 3840);
        theme.background.animationQuality = std::clamp(background.value("quality", 8), 2, 31);
        if (background.contains("embedded") && background["embedded"].is_object()) {
            const json embedded = background["embedded"];
            theme.background.imageEmbeddedName = embedded.value("name", "background.png");
            theme.background.imageEmbeddedMime = embedded.value("mime", "image/png");
            theme.background.imageEmbeddedBase64 = embedded.value("data", "");
            hydrateEmbeddedBackground(path, theme);
        }

        if (document.contains("colors") && document["colors"].is_object()) {
            for (auto it = document["colors"].begin(); it != document["colors"].end(); ++it) {
                ThemeColor color;
                color.key = it.key();
                if (readColor(it.value(), color)) {
                    theme.colors.push_back(color);
                }
            }
        }

        if (document.contains("metrics") && document["metrics"].is_object()) {
            for (auto it = document["metrics"].begin(); it != document["metrics"].end(); ++it) {
                if (!it.value().is_number()) {
                    continue;
                }
                ThemeColor color;
                color.key = it.key();
                color.color[0] = it.value().get<float>();
                color.color[1] = 0.0f;
                color.color[2] = 0.0f;
                color.color[3] = 0.0f;
                theme.colors.push_back(color);
            }
        }
    } catch (const std::exception& ex) {
        setError(error, ex.what());
        return false;
    }
    return true;
}

bool saveThemeFile(const ThemeDefinition& inputTheme, const std::filesystem::path& path, std::string* error)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        setError(error, ec.message());
        return false;
    }

    ThemeDefinition theme = inputTheme;
    embedBackgroundIfNeeded(theme);

    json colors = json::object();
    json metrics = json::object();
    for (const ThemeColor& color : theme.colors) {
        if (color.key.find("_rounding") != std::string::npos || color.key.find("_opacity") != std::string::npos ||
            color.key.find("_padding") != std::string::npos || color.key.find("_spacing") != std::string::npos ||
            color.key.find("_size") != std::string::npos || color.key.find("_border") != std::string::npos) {
            metrics[color.key] = color.color[0];
        } else {
            colors[color.key] = colorHex(color);
        }
    }

    json background = {{"enabled", theme.background.enabled},
                       {"image", theme.background.imageEmbeddedBase64.empty() ? storedThemePath(path, theme.background.imagePath)
                                                                              : theme.background.imageEmbeddedName},
                       {"mode", std::clamp(theme.background.imageMode, 0, 4)},
                       {"opacity", std::clamp(theme.background.opacity, 0, 100)}};
    if (theme.background.animated) {
        background["animated"] = true;
        background["fps"] = std::clamp(theme.background.animationFps, 1, 30);
        background["maxWidth"] = std::clamp(theme.background.animationMaxWidth, 240, 3840);
        background["quality"] = std::clamp(theme.background.animationQuality, 2, 31);
    }
    if (!theme.background.imageEmbeddedBase64.empty()) {
        background["embedded"] = {{"name", theme.background.imageEmbeddedName},
                                  {"mime", theme.background.imageEmbeddedMime},
                                  {"data", theme.background.imageEmbeddedBase64}};
    }

    json document;
    document["schemaVersion"] = kThemeSchemaVersion;
    document["id"] = unprefixedId(theme.id);
    document["name"] = theme.name;
    document["author"] = theme.author;
    document["dark"] = theme.dark;
    document["window"] = {{"transparent", theme.windowTransparent},
                          {"opacity", std::clamp(theme.windowOpacity, 1, 100)},
                          {"transparentForAll", theme.windowTransparencyForAll},
                          {"secondaryOpacity", std::clamp(theme.secondaryWindowOpacity, 1, 100)}};
    document["popup"] = {{"opacity", std::clamp(theme.popupMenuOpacity, 0, 100)}};
    document["background"] = std::move(background);
    document["colors"] = std::move(colors);
    document["metrics"] = std::move(metrics);

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        setError(error, "failed to write theme file");
        return false;
    }
    output << document.dump(2);
    if (!output) {
        setError(error, "failed to write theme file");
        return false;
    }
    return true;
}

void ThemeStore::reload(const std::filesystem::path& builtinDirectory, const std::filesystem::path& customDirectory,
                        const std::string& selectedId)
{
    builtinDirectory_ = builtinDirectory;
    customDirectory_ = customDirectory;
    themes_.clear();
    entries_.clear();

    std::error_code ec;
    std::filesystem::create_directories(customDirectory_, ec);

    loadDirectory(builtinDirectory_, true, themes_, entries_);
    loadDirectory(customDirectory_, false, themes_, entries_);
    if (themes_.empty()) {
        themes_.push_back(fallbackTheme(false));
        entries_.push_back({themes_.back().id, themes_.back().name, themes_.back().author, {}, true});
        themes_.push_back(fallbackTheme(true));
        entries_.push_back({themes_.back().id, themes_.back().name, themes_.back().author, {}, true});
    }

    const std::string wanted = selectedId.empty() ? "builtin:dark" : selectedId;
    if (!select(wanted)) {
        if (!select("builtin:dark") && !themes_.empty()) {
            active_ = themes_.front();
        }
    }
}

bool ThemeStore::select(const std::string& id)
{
    auto it = std::find_if(themes_.begin(), themes_.end(), [&](const ThemeDefinition& theme) {
        return theme.id == id;
    });
    if (it == themes_.end()) {
        return false;
    }
    active_ = *it;
    return true;
}

bool ThemeStore::getTheme(const std::string& id, ThemeDefinition& theme) const
{
    auto it = std::find_if(themes_.begin(), themes_.end(), [&](const ThemeDefinition& candidate) {
        return candidate.id == id;
    });
    if (it == themes_.end()) {
        return false;
    }
    theme = *it;
    return true;
}
void ThemeStore::setPreview(ThemeDefinition theme)
{
    active_ = std::move(theme);
}

bool ThemeStore::saveCustomTheme(const ThemeDefinition& theme, std::string* selectedId, std::string* error)
{
    ThemeDefinition custom = theme;
    const bool sourceWasBuiltin = custom.builtin || custom.id.rfind(kBuiltinPrefix, 0) == 0;
    const std::string rawBase = sourceWasBuiltin || custom.id.empty() ? custom.name : unprefixedId(custom.id);
    const std::string rawId = sanitizeFileStem(rawBase);
    custom.builtin = false;
    custom.id = prefixedId(false, rawId);
    custom.sourcePath = customDirectory_ / (rawId + ".json");
    if (!saveThemeFile(custom, custom.sourcePath, error)) {
        return false;
    }
    reload(builtinDirectory_, customDirectory_, custom.id);
    if (selectedId) {
        *selectedId = custom.id;
    }
    return true;
}

} // namespace launcher
