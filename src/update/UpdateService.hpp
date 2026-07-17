#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace launcher {

enum class UpdateState {
    Idle,
    Checking,
    UpToDate,
    Available,
    Downloading,
    ReadyToInstall,
    Installing,
    Failed,
};

struct UpdateSnapshot {
    UpdateState state = UpdateState::Idle;
    bool automaticCheck = false;
    std::string currentVersion;
    std::string latestVersion;
    std::string releaseNotes;
    std::string message;
    int downloadPercent = 0;
};

class UpdateService {
public:
    UpdateService();
    ~UpdateService();

    UpdateService(const UpdateService&) = delete;
    UpdateService& operator=(const UpdateService&) = delete;

    bool checkForUpdates(bool automatic = false);
    bool downloadUpdate();
    bool installDownloadedUpdate();
    UpdateSnapshot snapshot() const;

private:
    void checkWorker();
    void downloadWorker(std::wstring url, std::wstring checksumUrl, std::string packageName);
    void startWorker(std::thread worker);
    void setFailure(std::string message);

    mutable std::mutex mutex_;
    std::thread worker_;
    UpdateSnapshot snapshot_;
    std::filesystem::path downloadedPackage_;
    std::wstring pendingPackageUrl_;
    std::wstring pendingChecksumUrl_;
    std::string pendingPackageName_;
};

} // namespace launcher
