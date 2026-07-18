#include "ui/settings/ConfigTransfer.hpp"

#include "app/AppContext.hpp"
#include "core/StringEncoding.hpp"
#include "ui/common/Localization.hpp"
#include "ui/platform/UiPlatform.hpp"

#include <windows.h>

#include <exception>
#include <filesystem>
#include <string>

namespace {

void showMessage(const char* titleKey, const std::wstring& message, UINT icon)
{
    const std::wstring title = launcher::trw(titleKey);
    MessageBoxW(nullptr, message.c_str(), title.c_str(), MB_OK | icon);
}

std::wstring pathMessage(const char* prefixKey, const std::filesystem::path& path)
{
    std::wstring message = launcher::trw(prefixKey);
    message += L"\n";
    message += path.wstring();
    return message;
}

std::wstring errorMessage(const char* prefixKey, const std::string& error)
{
    std::wstring message = launcher::trw(prefixKey);
    if (!error.empty()) {
        message += L"\n";
        message += launcher::widen(error);
    }
    return message;
}

} // namespace

namespace launcher {

std::string configPathText(const AppContext& context)
{
    return context.config.path().string();
}

std::string configDirectoryText(const AppContext& context)
{
    return context.config.directory().string();
}

void openConfigLocation(AppContext& context)
{
    context.save();
    LaunchItem item;
    item.target = context.config.path();
    openContainingFolder(item);
}

void changeConfigDirectory(AppContext& context)
{
    const std::string selected = selectFolderDialog(trw("Select Config Directory").c_str(), context.config.directory());
    if (selected.empty()) {
        return;
    }

    context.save();
    std::string error;
    if (!context.config.moveConfigDirectory(std::filesystem::path(selected), &error)) {
        showMessage("Change Config Directory", errorMessage("Failed to change config directory.", error), MB_ICONERROR);
        return;
    }
    context.notes.setDirectory(context.config.directory() / "notes");
    context.notes.load();
    context.rebuildSearch();
    context.save();
    showMessage("Change Config Directory", pathMessage("Config directory changed to:", context.config.directory()), MB_ICONINFORMATION);
}

void exportConfig(AppContext& context)
{
    context.save();
    std::string output = saveFileDialog(trw("Export Config").c_str(), trw("JSON Config Filter").c_str());
    if (output.empty()) {
        return;
    }
    std::filesystem::path destination(output);
    if (!destination.has_extension()) {
        destination += ".json";
    }
    std::error_code ec;
    if (destination.has_parent_path()) {
        std::filesystem::create_directories(destination.parent_path(), ec);
    }
    if (ec) {
        showMessage("Export Config", errorMessage("Failed to create export directory.", ec.message()), MB_ICONERROR);
        return;
    }
    std::filesystem::copy_file(context.config.path(), destination, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        showMessage("Export Config", errorMessage("Failed to export config.", ec.message()), MB_ICONERROR);
        return;
    }
    showMessage("Export Config", pathMessage("Config exported to:", destination), MB_ICONINFORMATION);
}

void importConfig(AppContext& context)
{
    std::string input = openFileDialog(trw("Import Config").c_str(), trw("JSON Config Filter").c_str());
    if (input.empty()) {
        return;
    }

    const std::filesystem::path source(input);
    std::string loadError;
    if (!ConfigStore(source).tryLoadPersisted(&loadError)) {
        showMessage("Import Config", errorMessage("Selected config file is invalid.", loadError), MB_ICONERROR);
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(context.config.path().parent_path(), ec);
    if (ec) {
        showMessage("Import Config", errorMessage("Failed to create config directory.", ec.message()), MB_ICONERROR);
        return;
    }

    std::error_code ignored;
    const bool sameFile = std::filesystem::equivalent(source, context.config.path(), ignored);
    if (!sameFile) {
        std::filesystem::copy_file(source, context.config.path(), std::filesystem::copy_options::overwrite_existing, ec);
    }
    if (ec) {
        showMessage("Import Config", errorMessage("Failed to import config.", ec.message()), MB_ICONERROR);
        return;
    }

    try {
        context.load();
        context.save();
    } catch (const std::exception& ex) {
        showMessage("Import Config", errorMessage("Imported config could not be loaded.", ex.what()), MB_ICONERROR);
        return;
    }
    showMessage("Import Config", pathMessage("Config imported from:", source), MB_ICONINFORMATION);
}

} // namespace launcher
