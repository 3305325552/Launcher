#include "update/UpdateService.hpp"

#include "launcher/AppIdentity.hpp"

#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

namespace launcher {
namespace {

class InternetHandle {
public:
    explicit InternetHandle(HINTERNET handle = nullptr)
        : handle_(handle)
    {}
    ~InternetHandle()
    {
        if (handle_) {
            WinHttpCloseHandle(handle_);
        }
    }

    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
    HINTERNET get() const
    {
        return handle_;
    }

private:
    HINTERNET handle_ = nullptr;
};

std::wstring utf8ToWide(std::string_view value)
{
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::string wideToUtf8(std::wstring_view value)
{
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool endsWith(const std::string& value, std::string_view suffix)
{
    return value.size() >= suffix.size() && value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string trim(std::string value)
{
    const auto isSpace = [](unsigned char ch) {
        return std::isspace(ch) != 0;
    };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
                    return !isSpace(static_cast<unsigned char>(ch));
                }));
    value.erase(std::find_if(value.rbegin(), value.rend(),
                             [&](char ch) {
                                 return !isSpace(static_cast<unsigned char>(ch));
                             })
                    .base(),
                value.end());
    return value;
}

std::optional<std::string> proxyValueFromList(std::wstring value)
{
    const std::string proxyList = wideToUtf8(value);
    std::string httpProxy;
    std::string httpsProxy;
    std::string fallback;
    std::istringstream entries(proxyList);
    std::string entry;
    while (std::getline(entries, entry, ';')) {
        entry = trim(entry);
        if (entry.empty()) {
            continue;
        }
        const size_t separator = entry.find('=');
        if (separator == std::string::npos) {
            if (fallback.empty()) {
                fallback = entry;
            }
            continue;
        }
        const std::string scheme = lowercase(trim(entry.substr(0, separator)));
        const std::string proxy = trim(entry.substr(separator + 1));
        if (scheme == "https" && !proxy.empty()) {
            httpsProxy = proxy;
        } else if (scheme == "http" && !proxy.empty()) {
            httpProxy = proxy;
        }
        if (fallback.empty()) {
            fallback = proxy;
        }
    }
    if (!httpsProxy.empty()) {
        return httpsProxy;
    }
    if (!httpProxy.empty()) {
        return httpProxy;
    }
    if (fallback.empty()) {
        return std::nullopt;
    }
    return fallback;
}

std::optional<std::string> systemProxyForUrl(const std::wstring& url)
{
    WINHTTP_CURRENT_USER_IE_PROXY_CONFIG ieConfig{};
    if (!WinHttpGetIEProxyConfigForCurrentUser(&ieConfig)) {
        return std::nullopt;
    }

    std::optional<std::string> proxy;
    if (ieConfig.lpszProxy) {
        proxy = proxyValueFromList(ieConfig.lpszProxy);
    }

    if (!proxy && (ieConfig.fAutoDetect || ieConfig.lpszAutoConfigUrl)) {
        InternetHandle session(
            WinHttpOpen(L"Launcher-Updater-Proxy/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (session.get()) {
            WINHTTP_AUTOPROXY_OPTIONS options{};
            if (ieConfig.fAutoDetect) {
                options.dwFlags |= WINHTTP_AUTOPROXY_AUTO_DETECT;
                options.dwAutoDetectFlags = WINHTTP_AUTO_DETECT_TYPE_DHCP | WINHTTP_AUTO_DETECT_TYPE_DNS_A;
            }
            if (ieConfig.lpszAutoConfigUrl) {
                options.dwFlags |= WINHTTP_AUTOPROXY_CONFIG_URL;
                options.lpszAutoConfigUrl = ieConfig.lpszAutoConfigUrl;
            }
            WINHTTP_PROXY_INFO proxyInfo{};
            if (WinHttpGetProxyForUrl(session.get(), url.c_str(), &options, &proxyInfo) &&
                proxyInfo.dwAccessType == WINHTTP_ACCESS_TYPE_NAMED_PROXY && proxyInfo.lpszProxy) {
                proxy = proxyValueFromList(proxyInfo.lpszProxy);
            }
            if (proxyInfo.lpszProxy) {
                GlobalFree(proxyInfo.lpszProxy);
            }
            if (proxyInfo.lpszProxyBypass) {
                GlobalFree(proxyInfo.lpszProxyBypass);
            }
        }
    }

    if (ieConfig.lpszProxy) {
        GlobalFree(ieConfig.lpszProxy);
    }
    if (ieConfig.lpszProxyBypass) {
        GlobalFree(ieConfig.lpszProxyBypass);
    }
    if (ieConfig.lpszAutoConfigUrl) {
        GlobalFree(ieConfig.lpszAutoConfigUrl);
    }
    return proxy;
}

std::string windowsErrorMessage(DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::string message = length > 0 && buffer ? trim(wideToUtf8(std::wstring_view(buffer, length))) : std::string{};
    if (buffer) {
        LocalFree(buffer);
    }
    if (message.empty()) {
        message = "Windows error " + std::to_string(code);
    }
    return message;
}

bool crackHttpUrl(const std::wstring& url, std::wstring& host, std::wstring& path, INTERNET_PORT& port, bool& secure)
{
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components)) {
        return false;
    }
    if (components.nScheme != INTERNET_SCHEME_HTTP && components.nScheme != INTERNET_SCHEME_HTTPS) {
        return false;
    }
    host.assign(components.lpszHostName, components.dwHostNameLength);
    path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.lpszExtraInfo && components.dwExtraInfoLength > 0) {
        path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (path.empty()) {
        path = L"/";
    }
    port = components.nPort;
    secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    return !host.empty();
}

std::uint64_t responseContentLength(HINTERNET request)
{
    DWORD bytes = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &bytes, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t)) {
        return 0;
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, value.data(), &bytes,
                             WINHTTP_NO_HEADER_INDEX)) {
        return 0;
    }
    value.resize(bytes / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
    }
    try {
        return std::stoull(value);
    } catch (...) {
        return 0;
    }
}

bool streamHttp(const std::wstring& url, const std::function<bool(const BYTE*, DWORD)>& onData,
                const std::function<void(std::uint64_t)>& onSize, const std::function<void()>& onAttempt, std::string& error)
{
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port = 0;
    bool secure = false;
    if (!crackHttpUrl(url, host, path, port, secure)) {
        error = "The update URL is invalid.";
        return false;
    }

    const std::optional<std::string> proxy = systemProxyForUrl(url);
    std::string lastError;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const bool useProxy = attempt == 1;
        if (useProxy && !proxy) {
            break;
        }

        onAttempt();
        const std::wstring proxyName = useProxy ? utf8ToWide(*proxy) : std::wstring{};
        InternetHandle session(WinHttpOpen(L"Launcher-Updater/1.0",
                                           useProxy ? WINHTTP_ACCESS_TYPE_NAMED_PROXY : WINHTTP_ACCESS_TYPE_NO_PROXY,
                                           useProxy ? proxyName.c_str() : WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session.get()) {
            lastError = windowsErrorMessage(GetLastError());
            continue;
        }
        WinHttpSetTimeouts(session.get(), 15000, 15000, 30000, 30000);

        InternetHandle connection(WinHttpConnect(session.get(), host.c_str(), port, 0));
        if (!connection.get()) {
            lastError = windowsErrorMessage(GetLastError());
            continue;
        }
        InternetHandle request(WinHttpOpenRequest(connection.get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                  WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0));
        if (!request.get()) {
            lastError = windowsErrorMessage(GetLastError());
            continue;
        }
        if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.get(), nullptr)) {
            lastError = windowsErrorMessage(GetLastError());
            continue;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                 &status, &statusSize, WINHTTP_NO_HEADER_INDEX)) {
            lastError = windowsErrorMessage(GetLastError());
            continue;
        }
        if (status != 200) {
            lastError = "The update server returned HTTP " + std::to_string(status) + ".";
            continue;
        }

        onSize(responseContentLength(request.get()));
        bool receivedAll = true;
        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.get(), &available)) {
                lastError = windowsErrorMessage(GetLastError());
                receivedAll = false;
                break;
            }
            if (available == 0) {
                break;
            }
            std::vector<BYTE> buffer(std::min<DWORD>(available, 64 * 1024));
            DWORD received = 0;
            if (!WinHttpReadData(request.get(), buffer.data(), static_cast<DWORD>(buffer.size()), &received)) {
                lastError = windowsErrorMessage(GetLastError());
                receivedAll = false;
                break;
            }
            if (received > 0 && !onData(buffer.data(), received)) {
                lastError = error.empty() ? "The update response could not be processed." : error;
                receivedAll = false;
                break;
            }
        }

        if (receivedAll) {
            return true;
        }
    }

    error = lastError.empty() ? "The update server did not respond." : lastError;
    return false;
}

bool getText(const std::wstring& url, std::string& text, std::string& error)
{
    std::vector<BYTE> bytes;
    constexpr size_t kMaximumTextBytes = 4 * 1024 * 1024;
    if (!streamHttp(
            url,
            [&](const BYTE* data, DWORD length) {
                if (bytes.size() + length > kMaximumTextBytes) {
                    error = "The update response is too large.";
                    return false;
                }
                bytes.insert(bytes.end(), data, data + length);
                return true;
            },
            [](std::uint64_t) {},
            [&]() {
                bytes.clear();
                error.clear();
            },
            error)) {
        return false;
    }
    text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

std::optional<std::vector<unsigned int>> parseVersion(std::string value)
{
    if (!value.empty() && (value.front() == 'v' || value.front() == 'V')) {
        value.erase(value.begin());
    }
    const size_t suffix = value.find_first_of("-+");
    if (suffix != std::string::npos) {
        value.erase(suffix);
    }
    std::vector<unsigned int> parts;
    std::stringstream stream(value);
    std::string part;
    while (std::getline(stream, part, '.')) {
        if (part.empty() || !std::all_of(part.begin(), part.end(), [](unsigned char ch) {
                return std::isdigit(ch);
            })) {
            return std::nullopt;
        }
        try {
            parts.push_back(static_cast<unsigned int>(std::stoul(part)));
        } catch (...) {
            return std::nullopt;
        }
    }
    return parts.empty() ? std::nullopt : std::optional<std::vector<unsigned int>>(std::move(parts));
}

bool isNewerVersion(const std::string& remote, const std::string& local)
{
    const auto remoteParts = parseVersion(remote);
    const auto localParts = parseVersion(local);
    if (!remoteParts || !localParts) {
        return false;
    }
    const size_t count = std::max(remoteParts->size(), localParts->size());
    for (size_t index = 0; index < count; ++index) {
        const unsigned int remotePart = index < remoteParts->size() ? (*remoteParts)[index] : 0;
        const unsigned int localPart = index < localParts->size() ? (*localParts)[index] : 0;
        if (remotePart != localPart) {
            return remotePart > localPart;
        }
    }
    return false;
}

std::string calculateSha256(const std::filesystem::path& path, std::string& error)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<BYTE> object;
    std::array<BYTE, 32> result{};
    DWORD objectLength = 0;
    DWORD bytes = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &bytes, 0) < 0) {
        error = "SHA-256 is not available on this Windows installation.";
        if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    object.resize(objectLength);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0) {
        error = "Could not prepare SHA-256 verification.";
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }

    std::ifstream file(path, std::ios::binary);
    std::array<char, 64 * 1024> buffer{};
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = file.gcount();
        if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0) < 0) {
            error = "Could not verify the update checksum.";
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            return {};
        }
    }
    if (BCryptFinishHash(hash, result.data(), static_cast<ULONG>(result.size()), 0) < 0) {
        error = "Could not finish SHA-256 verification.";
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    static constexpr char kHex[] = "0123456789abcdef";
    std::string text;
    text.reserve(result.size() * 2);
    for (const BYTE byte : result) {
        text.push_back(kHex[byte >> 4]);
        text.push_back(kHex[byte & 0x0f]);
    }
    return text;
}

std::string checksumForPackage(const std::string& manifest, const std::string& packageName)
{
    std::istringstream lines(manifest);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream entry(line);
        std::string checksum;
        std::string name;
        if (entry >> checksum >> name) {
            if (!name.empty() && name.front() == '*') {
                name.erase(name.begin());
            }
            if (name == packageName && checksum.size() == 64) {
                return lowercase(checksum);
            }
        }
    }
    return {};
}

struct ReleasePackage {
    std::string name;
    std::string version;
};

std::optional<ReleasePackage> latestPackageFromManifest(const std::string& manifest)
{
    constexpr std::string_view kPackagePrefix = "launcher-";
    constexpr std::string_view kPackageSuffix = "-windows-x86_64.zip";
    std::istringstream lines(manifest);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream entry(line);
        std::string checksum;
        std::string name;
        if (!(entry >> checksum >> name)) {
            continue;
        }
        if (!name.empty() && name.front() == '*') {
            name.erase(name.begin());
        }
        if (checksum.size() != 64 || name.size() <= kPackagePrefix.size() + kPackageSuffix.size() || !name.starts_with(kPackagePrefix) ||
            !endsWith(name, kPackageSuffix)) {
            continue;
        }
        const size_t versionLength = name.size() - kPackagePrefix.size() - kPackageSuffix.size();
        return ReleasePackage{name, name.substr(kPackagePrefix.size(), versionLength)};
    }
    return std::nullopt;
}

std::wstring latestReleaseAssetUrl(const std::string& assetName)
{
    return std::wstring(app_identity::kLatestUpdateDownloadUrl) + utf8ToWide(assetName);
}

std::filesystem::path updateDownloadDirectory()
{
    PWSTR localAppData = nullptr;
    std::filesystem::path directory;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        directory = std::filesystem::path(localAppData) / app_identity::kDisplayName / "updates";
        CoTaskMemFree(localAppData);
    } else {
        directory = std::filesystem::temp_directory_path() / app_identity::kDisplayName / "updates";
    }
    return directory;
}

std::wstring quoted(const std::filesystem::path& path)
{
    return L"\"" + path.wstring() + L"\"";
}

} // namespace

UpdateService::UpdateService()
{
    snapshot_.currentVersion = app_identity::kVersion;
}

UpdateService::~UpdateService()
{
    if (worker_.joinable()) {
        worker_.join();
    }
}

void UpdateService::startWorker(std::thread worker)
{
    std::thread previous;
    {
        std::scoped_lock lock(mutex_);
        previous = std::move(worker_);
    }
    if (previous.joinable()) {
        previous.join();
    }
    std::scoped_lock lock(mutex_);
    worker_ = std::move(worker);
}

void UpdateService::setFailure(std::string message)
{
    std::scoped_lock lock(mutex_);
    snapshot_.state = UpdateState::Failed;
    snapshot_.message = std::move(message);
    snapshot_.downloadPercent = 0;
}

UpdateSnapshot UpdateService::snapshot() const
{
    std::scoped_lock lock(mutex_);
    return snapshot_;
}

bool UpdateService::checkForUpdates(bool automatic)
{
    {
        std::scoped_lock lock(mutex_);
        if (snapshot_.state == UpdateState::Checking || snapshot_.state == UpdateState::Downloading ||
            snapshot_.state == UpdateState::Installing) {
            return false;
        }
        snapshot_.state = UpdateState::Checking;
        snapshot_.automaticCheck = automatic;
        snapshot_.message.clear();
        snapshot_.latestVersion.clear();
        snapshot_.releaseNotes.clear();
        snapshot_.downloadPercent = 0;
        downloadedPackage_.clear();
    }
    startWorker(std::thread([this] {
        checkWorker();
    }));
    return true;
}

void UpdateService::checkWorker()
{
    std::string manifest;
    std::string error;
    const std::wstring checksumUrl = latestReleaseAssetUrl("SHA256SUMS.txt");
    if (!getText(checksumUrl, manifest, error)) {
        setFailure(std::move(error));
        return;
    }

    const std::optional<ReleasePackage> package = latestPackageFromManifest(manifest);
    if (!package) {
        setFailure("The latest update release is missing a compatible package or SHA256SUMS.txt.");
        return;
    }
    if (!isNewerVersion(package->version, app_identity::kVersion)) {
        std::scoped_lock lock(mutex_);
        snapshot_.state = UpdateState::UpToDate;
        snapshot_.latestVersion = package->version;
        snapshot_.message = "Launcher is up to date.";
        return;
    }

    std::scoped_lock lock(mutex_);
    snapshot_.state = UpdateState::Available;
    snapshot_.latestVersion = package->version;
    snapshot_.message = "A verified update is available.";
    downloadedPackage_.clear();
    pendingPackageUrl_ = latestReleaseAssetUrl(package->name);
    pendingChecksumUrl_ = checksumUrl;
    pendingPackageName_ = package->name;
}

bool UpdateService::downloadUpdate()
{
    std::wstring packageUrl;
    std::wstring checksumUrl;
    std::string packageName;
    {
        std::scoped_lock lock(mutex_);
        if (snapshot_.state != UpdateState::Available || pendingPackageUrl_.empty() || pendingChecksumUrl_.empty()) {
            return false;
        }
        snapshot_.state = UpdateState::Downloading;
        snapshot_.message = "Downloading update...";
        snapshot_.downloadPercent = 0;
        packageUrl = pendingPackageUrl_;
        checksumUrl = pendingChecksumUrl_;
        packageName = pendingPackageName_;
    }
    startWorker(
        std::thread([this, packageUrl = std::move(packageUrl), checksumUrl = std::move(checksumUrl), packageName = std::move(packageName)] {
            downloadWorker(packageUrl, checksumUrl, packageName);
        }));
    return true;
}

void UpdateService::downloadWorker(std::wstring url, std::wstring checksumUrl, std::string packageName)
{
    std::string error;
    std::string manifest;
    if (!getText(checksumUrl, manifest, error)) {
        setFailure("Could not download SHA256SUMS.txt: " + error);
        return;
    }
    const std::string expectedChecksum = checksumForPackage(manifest, packageName);
    if (expectedChecksum.empty()) {
        setFailure("SHA256SUMS.txt does not include the update package.");
        return;
    }

    const std::filesystem::path directory = updateDownloadDirectory();
    std::error_code filesystemError;
    std::filesystem::create_directories(directory, filesystemError);
    if (filesystemError) {
        setFailure("Could not create the update download directory.");
        return;
    }
    const std::filesystem::path destination = directory / utf8ToWide(packageName);
    const std::filesystem::path partial = destination.wstring() + L".part";
    std::ofstream file(partial, std::ios::binary | std::ios::trunc);
    if (!file) {
        setFailure("Could not create the update package file.");
        return;
    }

    std::uint64_t received = 0;
    std::uint64_t total = 0;
    const bool downloaded = streamHttp(
        url,
        [&](const BYTE* data, DWORD length) {
            file.write(reinterpret_cast<const char*>(data), length);
            if (!file) {
                error = "Could not write the update package.";
                return false;
            }
            received += length;
            if (total > 0) {
                std::scoped_lock lock(mutex_);
                snapshot_.downloadPercent = static_cast<int>(std::min<std::uint64_t>(100, received * 100 / total));
            }
            return true;
        },
        [&](std::uint64_t size) {
            total = size;
        },
        [&]() {
            file.close();
            file.open(partial, std::ios::binary | std::ios::trunc);
            received = 0;
            total = 0;
            error.clear();
        },
        error);
    file.close();
    if (!downloaded) {
        std::filesystem::remove(partial, filesystemError);
        setFailure(std::move(error));
        return;
    }

    const std::string actualChecksum = calculateSha256(partial, error);
    if (actualChecksum.empty() || lowercase(actualChecksum) != expectedChecksum) {
        std::filesystem::remove(partial, filesystemError);
        setFailure(actualChecksum.empty() ? std::move(error) : "The downloaded update failed SHA-256 verification.");
        return;
    }
    std::filesystem::remove(destination, filesystemError);
    std::filesystem::rename(partial, destination, filesystemError);
    if (filesystemError) {
        std::filesystem::remove(partial, filesystemError);
        setFailure("Could not finalize the verified update package.");
        return;
    }

    std::scoped_lock lock(mutex_);
    downloadedPackage_ = destination;
    snapshot_.state = UpdateState::ReadyToInstall;
    snapshot_.downloadPercent = 100;
    snapshot_.message = "The update was downloaded and verified.";
}

bool UpdateService::installDownloadedUpdate()
{
    std::filesystem::path package;
    {
        std::scoped_lock lock(mutex_);
        if (snapshot_.state != UpdateState::ReadyToInstall || downloadedPackage_.empty()) {
            return false;
        }
        package = downloadedPackage_;
    }

    wchar_t executable[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, executable, MAX_PATH) == 0) {
        setFailure("Could not locate the running Launcher executable.");
        return false;
    }
    const std::filesystem::path executablePath(executable);
    const std::filesystem::path updater = executablePath.parent_path() / L"launcher_updater.exe";
    if (!std::filesystem::exists(updater)) {
        setFailure("launcher_updater.exe is missing next to Launcher.");
        return false;
    }

    const std::wstring parameters = L"--pid " + std::to_wstring(GetCurrentProcessId()) + L" --package " + quoted(package) + L" --target " +
                                    quoted(executablePath.parent_path()) + L" --restart " + quoted(executablePath);
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", updater.c_str(), parameters.c_str(), executablePath.parent_path().c_str(), SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) <= 32) {
        setFailure("Could not start launcher_updater.exe.");
        return false;
    }

    std::scoped_lock lock(mutex_);
    snapshot_.state = UpdateState::Installing;
    snapshot_.message = "Restarting to install the verified update...";
    return true;
}

} // namespace launcher
