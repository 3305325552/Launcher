#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

std::optional<std::wstring> argumentValue(const std::vector<std::wstring>& arguments, const wchar_t* name)
{
    for (size_t index = 0; index + 1 < arguments.size(); ++index) {
        if (arguments[index] == name) {
            return arguments[index + 1];
        }
    }
    return std::nullopt;
}

std::wstring powerShellLiteral(std::wstring value)
{
    size_t offset = 0;
    while ((offset = value.find(L'\'', offset)) != std::wstring::npos) {
        value.insert(offset, 1, L'\'');
        offset += 2;
    }
    return L"'" + value + L"'";
}

bool expandArchive(const std::filesystem::path& package, const std::filesystem::path& staging)
{
    const std::wstring command =
        L"powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"Expand-Archive -LiteralPath " +
        powerShellLiteral(package.wstring()) + L" -DestinationPath " + powerShellLiteral(staging.wstring()) + L" -Force\"";
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        return false;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exitCode == 0;
}

bool copyReleaseFiles(const std::filesystem::path& source, const std::filesystem::path& target)
{
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator it(source, error), end; !error && it != end; it.increment(error)) {
        const std::filesystem::path relative = std::filesystem::relative(it->path(), source, error);
        if (error) {
            return false;
        }
        const std::filesystem::path destination = target / relative;
        if (it->is_directory(error)) {
            std::filesystem::create_directories(destination, error);
            if (error) {
                return false;
            }
            continue;
        }
        if (error || !it->is_regular_file(error)) {
            return false;
        }
        // The updater cannot overwrite the executable it is currently running from.
        if (_wcsicmp(relative.filename().c_str(), L"launcher_updater.exe") == 0) {
            continue;
        }
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error || !CopyFileW(it->path().c_str(), destination.c_str(), FALSE)) {
            return false;
        }
    }
    return !error;
}

void reportFailure(const std::filesystem::path& target, const wchar_t* message)
{
    std::ofstream log(target / L"launcher-update.log", std::ios::app);
    log << "Update failed: ";
    for (const wchar_t* cursor = message; *cursor; ++cursor) {
        log << static_cast<char>(*cursor < 128 ? *cursor : '?');
    }
    log << '\n';
    MessageBoxW(nullptr, message, L"Launcher update", MB_OK | MB_ICONERROR);
}

} // namespace

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    int count = 0;
    LPWSTR* rawArguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!rawArguments) {
        return 1;
    }
    std::vector<std::wstring> arguments(rawArguments, rawArguments + count);
    LocalFree(rawArguments);

    const auto processId = argumentValue(arguments, L"--pid");
    const auto package = argumentValue(arguments, L"--package");
    const auto target = argumentValue(arguments, L"--target");
    const auto restart = argumentValue(arguments, L"--restart");
    if (!processId || !package || !target || !restart) {
        MessageBoxW(nullptr, L"The update command is incomplete.", L"Launcher update", MB_OK | MB_ICONERROR);
        return 1;
    }

    DWORD pid = 0;
    try {
        pid = static_cast<DWORD>(std::stoul(*processId));
    } catch (...) {
        reportFailure(*target, L"The update command contains an invalid process id.");
        return 1;
    }
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (process) {
        const DWORD waitResult = WaitForSingleObject(process, 120000);
        CloseHandle(process);
        if (waitResult != WAIT_OBJECT_0) {
            reportFailure(*target, L"Launcher did not exit in time, so the update was cancelled.");
            return 1;
        }
    }

    const std::filesystem::path staging =
        std::filesystem::temp_directory_path() /
        (L"launcher-update-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    std::filesystem::create_directories(staging, error);
    if (error || !expandArchive(*package, staging)) {
        std::filesystem::remove_all(staging, error);
        reportFailure(*target, L"Could not extract the downloaded update package.");
        return 1;
    }

    std::filesystem::path releaseDirectory;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(staging, error)) {
        if (!error && entry.is_directory(error)) {
            releaseDirectory = entry.path();
            break;
        }
    }
    if (error || releaseDirectory.empty() || !copyReleaseFiles(releaseDirectory, *target)) {
        std::filesystem::remove_all(staging, error);
        reportFailure(*target, L"Could not replace Launcher files. Check write permissions for the install folder.");
        return 1;
    }

    std::filesystem::remove_all(staging, error);
    ShellExecuteW(nullptr, L"open", restart->c_str(), nullptr, target->c_str(), SW_SHOWNORMAL);
    return 0;
}
