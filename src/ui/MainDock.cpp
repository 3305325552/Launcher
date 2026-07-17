#include "ui/MainDock.hpp"

#include "app/AppContext.hpp"
#include "core/AnimatedBackground.hpp"
#include "core/ModelActions.hpp"
#include "launcher/AppIdentity.hpp"
#include "ui/ConfigTransfer.hpp"
#include "ui/Localization.hpp"
#include "ui/MainDockCategoryRail.hpp"
#include "ui/MainDockChrome.hpp"
#include "ui/MainDockContextMenus.hpp"
#include "ui/MainDockDialogs.hpp"
#include "ui/MainDockGrid.hpp"
#include "ui/MainDockItemEditor.hpp"
#include "ui/MainDockItemViews.hpp"
#include "ui/MainDockMenu.hpp"
#include "ui/MainDockNotes.hpp"
#include "ui/MainDockSearch.hpp"
#include "ui/MainDockState.hpp"
#include "ui/MainDockWin32.hpp"
#include "ui/MaterialIcons.hpp"
#include "ui/SettingsPanel.hpp"
#include "ui/ThemeEditor.hpp"
#include "ui/UiAnimation.hpp"
#include "ui/UiTheme.hpp"
#include "ui/UserGuideView.hpp"

#include <windows.h>
#include <commdlg.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <d3d11.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace launcher {
namespace {

using model_actions::findItemInList;
using model_actions::itemIndexById;

constexpr float kTitleHeight = kUiTitleHeight;
constexpr float kSearchHeight = 36.0f;
constexpr float kRailWidth = 164.0f;
constexpr int kMinWindowWidth = 720;
constexpr int kMinWindowHeight = 520;
constexpr int kIconLoadPauseFramesAfterScopeChange = 2;
constexpr int kSearchIconLoadPauseFramesAfterScopeChange = 18;
constexpr int kIconLoadPauseFramesAfterScroll = 4;
constexpr int kMaxIconTextureLoadsPerFrame = 2;
constexpr int kMaxSearchIconTextureLoadsPerFrame = 1;
constexpr size_t kMaxIconTextureCacheBytes = 2 * 1024 * 1024;
constexpr size_t kMaxIconTextureCacheEntries = 256;
constexpr size_t kMaxPendingIconRequests = 96;
constexpr int kFailedIconRetryFrames = 240;
constexpr int kIconWorkingSetTrimIdleFrames = 6;
constexpr int kIconWorkingSetTrimMinIntervalFrames = 120;
constexpr size_t kMaxInteractiveParamHistory = 32;
constexpr int kMaxInteractiveHistorySuggestions = 8;

struct IconTexture {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    int width = 0;
    int height = 0;
};

struct CachedIconTexture {
    std::unique_ptr<IconTexture> texture;
    size_t byteSize = 0;
    int lastUsedFrame = 0;
};

struct IconTextureCache {
    std::unordered_map<std::string, CachedIconTexture> textures;
    std::deque<LaunchItem> pendingRequests;
    std::unordered_set<std::string> pendingRequestKeys;
    std::unordered_map<std::string, int> failedRequestFrame;
    std::string loadScopeKey;
    size_t totalBytes = 0;
    int loadPauseFrames = 0;
    int scrollPauseFrames = 0;
    int loadBudgetThisFrame = kMaxIconTextureLoadsPerFrame;
    bool workingSetTrimPending = false;
    int lastIconLoadFrame = 0;
    int lastWorkingSetTrimFrame = -kIconWorkingSetTrimMinIntervalFrames;
};

struct BackgroundTextureCache {
    std::string key;
    std::vector<std::filesystem::path> frames;
    std::unique_ptr<IconTexture> texture;
    int frameIndex = -1;
};

struct InteractiveHistoryCandidate {
    std::string value;
    int useCount = 0;
    std::int64_t lastUsedAt = 0;
    int sourceIndex = -1;
    bool prefixMatch = false;
};

struct MainDockSession {
    bool focusSearch = false;
    bool searchOpen = false;
    bool showItemEditor = false;
    bool openItemEditorPopup = false;
    bool openSettingsNextFrame = false;
    int editingCategory = -1;
    int editingItem = -1;
    std::string editingFolderId;
    LaunchItem editingDraft;
    std::string editingTarget;
    std::string editingStartDir;
    std::string editingRemark;
    std::string editingIcon;
    bool showInteractiveRun = false;
    bool openInteractiveRunPopup = false;
    LaunchItem interactiveRunItem;
    std::string interactiveRunItemId;
    std::string interactiveRunSearchText;
    int interactiveRunShowCommand = SW_SHOWNORMAL;
    std::vector<std::string> interactiveRunValues;
    std::string interactiveHistoryParamKey;
    int interactiveHistorySelected = -1;
    bool searchSubmit = false;
    int searchSelected = 0;
    int searchMove = 0;
    int searchPageMove = 0;
    std::string searchQueryText;
    double searchEditedAt = 0.0;
    bool searchCursorEndRequested = false;
    SearchResultsCache searchResultsCache;
    ID3D11Device* d3dDevice = nullptr;
    bool showBuildInfo = false;
    bool showTaskPlanner = false;
    bool showUpdateDialog = false;
    std::string automaticUpdatePromptVersion;
    bool openCategoryEditorPopup = false;
    int editingCategoryIndex = -1;
    std::string editingCategoryName;
    std::string editingCategoryIconName;
    std::string editingCategoryIconColor;
    std::string categoryIconFilter;
    bool draggingMainWindow = false;
    ImVec2 mainDragStartMouse{};
    RECT mainDragStartRect{};
    bool resetMainDockScroll = false;
    bool themeEditorWasOpen = false;
    bool useDefaultIcons = false;
    IconTextureCache iconCache;
    BackgroundTextureCache backgroundTexture;
    UiPalette theme = uiPalette(ThemeDefinition{});
};

MainDockSession gSession;

std::filesystem::path resolveExecutablePath(const std::filesystem::path& target)
{
    if (target.empty()) {
        return {};
    }
    std::error_code ec;
    if (std::filesystem::exists(target, ec)) {
        return target;
    }

    const std::wstring targetText = target.wstring();
    wchar_t resolved[MAX_PATH]{};
    if (SearchPathW(nullptr, targetText.c_str(), nullptr, MAX_PATH, resolved, nullptr) > 0) {
        return resolved;
    }
    return target;
}

bool isGlobalFileItem(const LaunchItem& item)
{
    return item.icon.empty() && item.id.starts_with("global-file:");
}

bool isGlobalFileDirectoryItem(const LaunchItem& item)
{
    return isGlobalFileItem(item) && item.startDirectory == item.target;
}

std::string lowercaseAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string pathExtensionKey(const std::filesystem::path& path)
{
    std::string extension = lowercaseAscii(path.extension().string());
    return extension.empty() ? "<none>" : extension;
}

bool extensionMayHavePerFileIcon(std::string_view extension)
{
    return extension == ".exe" || extension == ".lnk" || extension == ".url" || extension == ".ico" || extension == ".cur" ||
           extension == ".ani" || extension == ".scr" || extension == ".cpl" || extension == ".msc" || extension == ".dll";
}

bool useSharedGlobalFileIcon(const LaunchItem& item)
{
    if (!isGlobalFileItem(item)) {
        return false;
    }
    if (isGlobalFileDirectoryItem(item)) {
        return true;
    }
    const std::string extension = pathExtensionKey(item.target);
    return !extensionMayHavePerFileIcon(extension);
}

std::string iconCacheKey(const LaunchItem& item)
{
    if (!item.icon.empty()) {
        return "icon:" + item.icon;
    }
    if (isGlobalFileItem(item)) {
        if (isGlobalFileDirectoryItem(item)) {
            return "global-file:directory";
        }
        const std::string extension = pathExtensionKey(item.target);
        if (!extensionMayHavePerFileIcon(extension)) {
            return "global-file:extension:" + extension;
        }
    }
    return "target:" + item.target.string() + "|type:" + std::to_string(static_cast<int>(item.type));
}

DWORD shellIconAttributesForItem(const LaunchItem& item, const std::filesystem::path& source)
{
    if (isGlobalFileDirectoryItem(item)) {
        return FILE_ATTRIBUTE_DIRECTORY;
    }

    std::error_code ec;
    if (std::filesystem::is_directory(source, ec)) {
        return FILE_ATTRIBUTE_DIRECTORY;
    }
    return FILE_ATTRIBUTE_NORMAL;
}

HICON shellIconForItem(const LaunchItem& item)
{
    const std::string targetText = item.target.string();
    if (item.icon.empty() && (targetText.starts_with("http://") || targetText.starts_with("https://"))) {
        return nullptr;
    }

    const bool sharedGlobalIcon = useSharedGlobalFileIcon(item);
    std::filesystem::path source = item.icon.empty() ? item.target : std::filesystem::path(item.icon);
    if (!sharedGlobalIcon) {
        source = resolveExecutablePath(source);
    }
    if (source.empty()) {
        return nullptr;
    }

    const DWORD attributes = shellIconAttributesForItem(item, source);
    std::error_code ec;
    const bool exists = !sharedGlobalIcon && std::filesystem::exists(source, ec);
    const bool shortcutIcon = item.icon.empty() && lowercaseAscii(source.extension().string()) == ".lnk";

    auto directIcon = [&](UINT extraFlags) -> HICON {
        SHFILEINFOW info{};
        const UINT flags = SHGFI_ICON | SHGFI_LARGEICON | extraFlags;
        if (SHGetFileInfoW(source.wstring().c_str(), attributes, &info, sizeof(info), flags) == 0) {
            return nullptr;
        }
        return info.hIcon;
    };

    if (!shortcutIcon) {
        if (HICON icon = directIcon(exists ? 0 : SHGFI_USEFILEATTRIBUTES)) {
            return icon;
        }
        if (exists) {
            if (HICON icon = directIcon(SHGFI_USEFILEATTRIBUTES)) {
                return icon;
            }
        }
    }

    auto iconFromImageList = [&](int imageListSize, int iconIndex) -> HICON {
        Microsoft::WRL::ComPtr<IImageList> imageList;
        if (FAILED(SHGetImageList(imageListSize, IID_PPV_ARGS(imageList.GetAddressOf())))) {
            return nullptr;
        }
        HICON icon = nullptr;
        if (FAILED(imageList->GetIcon(iconIndex, ILD_TRANSPARENT, &icon))) {
            return nullptr;
        }
        return icon;
    };

    auto iconFromSystemImageList = [&](UINT extraFlags) -> HICON {
        SHFILEINFOW info{};
        const UINT flags = SHGFI_SYSICONINDEX | SHGFI_LARGEICON | extraFlags;
        if (SHGetFileInfoW(source.wstring().c_str(), attributes, &info, sizeof(info), flags) == 0) {
            return nullptr;
        }
        if (HICON icon = iconFromImageList(SHIL_EXTRALARGE, info.iIcon)) {
            return icon;
        }
        if (HICON icon = iconFromImageList(SHIL_LARGE, info.iIcon)) {
            return icon;
        }
        return nullptr;
    };

    if (HICON icon = iconFromSystemImageList(exists ? 0 : SHGFI_USEFILEATTRIBUTES)) {
        return icon;
    }
    if (exists) {
        if (HICON icon = iconFromSystemImageList(SHGFI_USEFILEATTRIBUTES)) {
            return icon;
        }
    }
    return nullptr;
}

struct IconBitmapSize {
    int width = 64;
    int height = 64;
};

struct IconContentBounds {
    int left = 0;
    int top = 0;
    int right = -1;
    int bottom = -1;

    bool valid() const
    {
        return right >= left && bottom >= top;
    }

    int width() const
    {
        return right - left + 1;
    }

    int height() const
    {
        return bottom - top + 1;
    }
};

IconBitmapSize bitmapSizeForIcon(HICON icon)
{
    ICONINFO info{};
    if (!GetIconInfo(icon, &info)) {
        return {};
    }

    auto cleanup = [&]() {
        if (info.hbmColor) {
            DeleteObject(info.hbmColor);
        }
        if (info.hbmMask) {
            DeleteObject(info.hbmMask);
        }
    };

    BITMAP bitmap{};
    IconBitmapSize result{};
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
        result.width = bitmap.bmWidth;
        result.height = bitmap.bmHeight;
    } else if (info.hbmMask && GetObjectW(info.hbmMask, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
        result.width = bitmap.bmWidth;
        result.height = bitmap.bmHeight / 2;
    }
    cleanup();

    result.width = std::clamp(result.width, 16, 256);
    result.height = std::clamp(result.height, 16, 256);
    return result;
}

std::vector<std::uint32_t> rasterizeIcon(HICON icon, int width, int height)
{
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!dc || !bitmap || !bits) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
        if (dc) {
            DeleteDC(dc);
        }
        return {};
    }

    HGDIOBJ old = SelectObject(dc, bitmap);
    std::fill_n(static_cast<std::uint32_t*>(bits), width * height, 0);
    DrawIconEx(dc, 0, 0, icon, width, height, 0, nullptr, DI_NORMAL);
    SelectObject(dc, old);

    std::vector<std::uint32_t> rgba(static_cast<size_t>(width) * height);
    const auto* bgra = static_cast<const std::uint8_t*>(bits);
    for (int i = 0; i < width * height; ++i) {
        const std::uint8_t b = bgra[i * 4 + 0];
        const std::uint8_t g = bgra[i * 4 + 1];
        const std::uint8_t r = bgra[i * 4 + 2];
        std::uint8_t a = bgra[i * 4 + 3];
        if (a == 0 && (r != 0 || g != 0 || b != 0)) {
            a = 255;
        }
        rgba[i] = (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) | (static_cast<std::uint32_t>(g) << 8) |
                  static_cast<std::uint32_t>(r);
    }

    DeleteObject(bitmap);
    DeleteDC(dc);
    return rgba;
}

IconContentBounds visibleIconBounds(const std::vector<std::uint32_t>& rgba, int width, int height)
{
    IconContentBounds bounds{width, height, -1, -1};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t alpha = static_cast<std::uint8_t>(rgba[static_cast<size_t>(y) * width + x] >> 24);
            if (alpha <= 8) {
                continue;
            }
            bounds.left = std::min(bounds.left, x);
            bounds.top = std::min(bounds.top, y);
            bounds.right = std::max(bounds.right, x);
            bounds.bottom = std::max(bounds.bottom, y);
        }
    }
    return bounds;
}

bool shouldNormalizeIconBounds(const IconContentBounds& bounds, int width, int height)
{
    if (!bounds.valid()) {
        return false;
    }
    const float sourceMax = static_cast<float>(std::max(width, height));
    const float contentMax = static_cast<float>(std::max(bounds.width(), bounds.height()));
    const float contentCenterX = (static_cast<float>(bounds.left + bounds.right) + 1.0f) * 0.5f;
    const float contentCenterY = (static_cast<float>(bounds.top + bounds.bottom) + 1.0f) * 0.5f;
    const float sourceCenterX = static_cast<float>(width) * 0.5f;
    const float sourceCenterY = static_cast<float>(height) * 0.5f;
    const bool verySmall = contentMax <= sourceMax * 0.35f;
    const bool smallContent = contentMax <= sourceMax * 0.64f;
    const bool offCenter =
        std::abs(contentCenterX - sourceCenterX) > sourceMax * 0.12f || std::abs(contentCenterY - sourceCenterY) > sourceMax * 0.12f;
    const bool anchoredTopLeft =
        bounds.left <= width * 0.04f && bounds.top <= height * 0.04f && (bounds.right < width * 0.70f || bounds.bottom < height * 0.70f);
    return verySmall || (smallContent && offCenter) || anchoredTopLeft;
}

std::uint32_t sampleIconPixel(const std::vector<std::uint32_t>& rgba, int width, int height, float x, float y)
{
    x = std::clamp(x, 0.0f, static_cast<float>(width - 1));
    y = std::clamp(y, 0.0f, static_cast<float>(height - 1));
    const int x0 = std::clamp(static_cast<int>(std::floor(x)), 0, width - 1);
    const int y0 = std::clamp(static_cast<int>(std::floor(y)), 0, height - 1);
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const float tx = x - static_cast<float>(x0);
    const float ty = y - static_cast<float>(y0);

    const auto pixel = [&](int px, int py) {
        return rgba[static_cast<size_t>(py) * width + px];
    };
    const std::uint32_t samples[4] = {pixel(x0, y0), pixel(x1, y0), pixel(x0, y1), pixel(x1, y1)};
    const float weights[4] = {(1.0f - tx) * (1.0f - ty), tx * (1.0f - ty), (1.0f - tx) * ty, tx * ty};

    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
    for (int i = 0; i < 4; ++i) {
        r += static_cast<float>(samples[i] & 0xFF) * weights[i];
        g += static_cast<float>((samples[i] >> 8) & 0xFF) * weights[i];
        b += static_cast<float>((samples[i] >> 16) & 0xFF) * weights[i];
        a += static_cast<float>((samples[i] >> 24) & 0xFF) * weights[i];
    }

    return (static_cast<std::uint32_t>(std::clamp(std::lround(a), 0L, 255L)) << 24) |
           (static_cast<std::uint32_t>(std::clamp(std::lround(b), 0L, 255L)) << 16) |
           (static_cast<std::uint32_t>(std::clamp(std::lround(g), 0L, 255L)) << 8) |
           static_cast<std::uint32_t>(std::clamp(std::lround(r), 0L, 255L));
}

std::vector<std::uint32_t> scaleIconPixels(const std::vector<std::uint32_t>& source, int sourceW, int sourceH, int targetSize)
{
    if (source.empty() || sourceW <= 0 || sourceH <= 0 || targetSize <= 0) {
        return {};
    }

    IconContentBounds sourceRect{0, 0, sourceW - 1, sourceH - 1};
    const IconContentBounds bounds = visibleIconBounds(source, sourceW, sourceH);
    const bool normalize = shouldNormalizeIconBounds(bounds, sourceW, sourceH);
    if (normalize) {
        sourceRect = bounds;
    }

    std::vector<std::uint32_t> target(static_cast<size_t>(targetSize) * targetSize, 0);
    const int srcW = sourceRect.width();
    const int srcH = sourceRect.height();
    if (srcW <= 0 || srcH <= 0) {
        return target;
    }

    int dstW = targetSize;
    int dstH = targetSize;
    int dstX = 0;
    int dstY = 0;
    if (normalize) {
        const int maxDraw = std::max(1, targetSize - 8);
        const float scale =
            std::min(static_cast<float>(maxDraw) / static_cast<float>(srcW), static_cast<float>(maxDraw) / static_cast<float>(srcH));
        dstW = std::clamp(static_cast<int>(std::lround(srcW * scale)), 1, targetSize);
        dstH = std::clamp(static_cast<int>(std::lround(srcH * scale)), 1, targetSize);
        dstX = (targetSize - dstW) / 2;
        dstY = (targetSize - dstH) / 2;
    }

    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            const float srcX = static_cast<float>(sourceRect.left) + (static_cast<float>(x) + 0.5f) * srcW / dstW - 0.5f;
            const float srcY = static_cast<float>(sourceRect.top) + (static_cast<float>(y) + 0.5f) * srcH / dstH - 0.5f;
            target[static_cast<size_t>(dstY + y) * targetSize + (dstX + x)] = sampleIconPixel(source, sourceW, sourceH, srcX, srcY);
        }
    }
    return target;
}

std::unique_ptr<IconTexture> createTextureFromIcon(HICON icon)
{
    if (!gSession.d3dDevice || !icon) {
        return {};
    }

    constexpr int iconSize = 48;
    const IconBitmapSize sourceSize = bitmapSizeForIcon(icon);
    std::vector<std::uint32_t> rgba =
        scaleIconPixels(rasterizeIcon(icon, sourceSize.width, sourceSize.height), sourceSize.width, sourceSize.height, iconSize);
    if (rgba.empty()) {
        return {};
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = iconSize;
    desc.Height = iconSize;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = rgba.data();
    data.SysMemPitch = iconSize * 4;

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(gSession.d3dDevice->CreateTexture2D(&desc, &data, texture.GetAddressOf()))) {
        return {};
    }

    auto result = std::make_unique<IconTexture>();
    result->width = iconSize;
    result->height = iconSize;
    if (FAILED(gSession.d3dDevice->CreateShaderResourceView(texture.Get(), nullptr, result->srv.GetAddressOf()))) {
        return {};
    }
    return result;
}

std::string iconLoadScopeKey(const AppContext& context)
{
    if (gSession.searchOpen) {
        return "search:" + context.runtime().searchText + "|" + gSession.searchQueryText;
    }

    std::string key = "category:" + std::to_string(context.runtime().selectedCategory);
    if (context.runtime().selectedCategory >= 0 &&
        context.runtime().selectedCategory < static_cast<int>(context.persisted().categories.size())) {
        key += ":" + context.persisted().categories[context.runtime().selectedCategory].id;
    }
    for (const std::string& folderId : context.runtime().currentFolderStack) {
        key += "/" + folderId;
    }
    return key;
}

void resetIconLoadScheduling()
{
    gSession.iconCache.loadScopeKey.clear();
    gSession.iconCache.pendingRequests.clear();
    gSession.iconCache.pendingRequestKeys.clear();
    gSession.iconCache.failedRequestFrame.clear();
    gSession.iconCache.loadPauseFrames = 0;
    gSession.iconCache.scrollPauseFrames = 0;
    gSession.iconCache.loadBudgetThisFrame = kMaxIconTextureLoadsPerFrame;
    gSession.iconCache.workingSetTrimPending = false;
    gSession.iconCache.lastIconLoadFrame = 0;
}

void trimCurrentProcessWorkingSet();
void clearIconTextureCache();

void beginIconLoadFrame(const AppContext& context)
{
    if (context.persisted().settings.useDefaultIcons) {
        if (!gSession.iconCache.textures.empty() || !gSession.iconCache.pendingRequests.empty()) {
            clearIconTextureCache();
            trimCurrentProcessWorkingSet();
        }
        gSession.iconCache.loadScopeKey = "default-icons";
        gSession.iconCache.loadBudgetThisFrame = 0;
        return;
    }

    const std::string scopeKey = iconLoadScopeKey(context);
    if (scopeKey != gSession.iconCache.loadScopeKey) {
        const bool hadCachedIcons = !gSession.iconCache.textures.empty();
        clearIconTextureCache();
        if (hadCachedIcons) {
            trimCurrentProcessWorkingSet();
        }
        gSession.iconCache.loadScopeKey = scopeKey;
        gSession.iconCache.loadPauseFrames =
            gSession.searchOpen ? kSearchIconLoadPauseFramesAfterScopeChange : kIconLoadPauseFramesAfterScopeChange;
    }

    if (gSession.iconCache.loadPauseFrames > 0) {
        --gSession.iconCache.loadPauseFrames;
        gSession.iconCache.loadBudgetThisFrame = 0;
        return;
    }

    ImGuiIO& io = ImGui::GetIO();
    if (gSession.searchOpen && std::abs(io.MouseWheel) > 0.0f) {
        gSession.iconCache.scrollPauseFrames = kIconLoadPauseFramesAfterScroll;
    }
    if (gSession.iconCache.scrollPauseFrames > 0) {
        --gSession.iconCache.scrollPauseFrames;
        gSession.iconCache.loadBudgetThisFrame = 0;
        return;
    }

    gSession.iconCache.loadBudgetThisFrame = gSession.searchOpen ? kMaxSearchIconTextureLoadsPerFrame : kMaxIconTextureLoadsPerFrame;
}

size_t iconTextureByteSize(const IconTexture* texture)
{
    if (!texture || texture->width <= 0 || texture->height <= 0) {
        return 0;
    }
    return static_cast<size_t>(texture->width) * static_cast<size_t>(texture->height) * 4;
}

void eraseIconTextureCacheEntry(const std::string& key)
{
    auto it = gSession.iconCache.textures.find(key);
    if (it == gSession.iconCache.textures.end()) {
        return;
    }
    gSession.iconCache.totalBytes -= std::min(gSession.iconCache.totalBytes, it->second.byteSize);
    gSession.iconCache.textures.erase(it);
}

void trimCurrentProcessWorkingSet()
{
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

void markIconWorkingSetDirty()
{
    gSession.iconCache.workingSetTrimPending = true;
    gSession.iconCache.lastIconLoadFrame = ImGui::GetFrameCount();
}

void maybeTrimIconWorkingSet()
{
    if (!gSession.iconCache.workingSetTrimPending || !gSession.iconCache.pendingRequests.empty()) {
        return;
    }

    const int frame = ImGui::GetFrameCount();
    if (frame - gSession.iconCache.lastIconLoadFrame < kIconWorkingSetTrimIdleFrames) {
        return;
    }
    if (frame - gSession.iconCache.lastWorkingSetTrimFrame < kIconWorkingSetTrimMinIntervalFrames) {
        return;
    }

    trimCurrentProcessWorkingSet();
    gSession.iconCache.workingSetTrimPending = false;
    gSession.iconCache.lastWorkingSetTrimFrame = frame;
}

void clearIconTextureCache()
{
    gSession.iconCache.textures.clear();
    gSession.iconCache.pendingRequests.clear();
    gSession.iconCache.pendingRequestKeys.clear();
    gSession.iconCache.failedRequestFrame.clear();
    gSession.iconCache.totalBytes = 0;
    gSession.iconCache.workingSetTrimPending = false;
}

void pruneIconTextureCache()
{
    if (gSession.iconCache.textures.empty()) {
        return;
    }

    const int currentFrame = ImGui::GetFrameCount();
    while (
        (gSession.iconCache.totalBytes > kMaxIconTextureCacheBytes || gSession.iconCache.textures.size() > kMaxIconTextureCacheEntries) &&
        !gSession.iconCache.textures.empty()) {
        auto candidate = gSession.iconCache.textures.end();
        for (auto it = gSession.iconCache.textures.begin(); it != gSession.iconCache.textures.end(); ++it) {
            if (it->second.lastUsedFrame == currentFrame && gSession.iconCache.textures.size() <= kMaxIconTextureCacheEntries) {
                continue;
            }
            if (candidate == gSession.iconCache.textures.end() || it->second.lastUsedFrame < candidate->second.lastUsedFrame) {
                candidate = it;
            }
        }
        if (candidate == gSession.iconCache.textures.end()) {
            break;
        }
        gSession.iconCache.totalBytes -= std::min(gSession.iconCache.totalBytes, candidate->second.byteSize);
        gSession.iconCache.textures.erase(candidate);
    }
}
bool canCreateIconTextureThisFrame()
{
    if (gSession.iconCache.loadBudgetThisFrame <= 0) {
        return false;
    }
    --gSession.iconCache.loadBudgetThisFrame;
    return true;
}

IconTexture* iconTextureForItem(const LaunchItem& item)
{
    if (gSession.useDefaultIcons || !gSession.d3dDevice || item.type == LaunchItemType::Placeholder || item.type == LaunchItemType::Title) {
        return nullptr;
    }

    const std::string key = iconCacheKey(item);
    const int frame = ImGui::GetFrameCount();
    if (auto it = gSession.iconCache.textures.find(key); it != gSession.iconCache.textures.end()) {
        it->second.lastUsedFrame = frame;
        return it->second.texture.get();
    }

    if (!canCreateIconTextureThisFrame()) {
        return nullptr;
    }

    HICON icon = shellIconForItem(item);
    markIconWorkingSetDirty();
    std::unique_ptr<IconTexture> texture = createTextureFromIcon(icon);
    if (icon) {
        DestroyIcon(icon);
    }
    if (!texture || !texture->srv) {
        return nullptr;
    }

    CachedIconTexture entry;
    entry.byteSize = iconTextureByteSize(texture.get());
    entry.lastUsedFrame = frame;
    entry.texture = std::move(texture);
    IconTexture* result = entry.texture.get();
    gSession.iconCache.totalBytes += entry.byteSize;
    gSession.iconCache.textures.emplace(key, std::move(entry));
    pruneIconTextureCache();
    return result;
}

bool drawCachedLaunchIcon(const LaunchItem& item, const ImVec2& pos, float size)
{
    if (gSession.useDefaultIcons) {
        return false;
    }
    const std::string key = iconCacheKey(item);
    auto it = gSession.iconCache.textures.find(key);
    if (it == gSession.iconCache.textures.end() || !it->second.texture || !it->second.texture->srv) {
        return false;
    }

    it->second.lastUsedFrame = ImGui::GetFrameCount();
    const float rounding = item.type == LaunchItemType::Url ? size * 0.5f : 8.0f;
    ImGui::GetWindowDrawList()->AddImageRounded(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(it->second.texture->srv.Get())),
                                                pos, ImVec2(pos.x + size, pos.y + size), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                                                IM_COL32_WHITE, rounding);
    return true;
}

void requestLaunchIcon(const LaunchItem& item)
{
    if (gSession.useDefaultIcons || !gSession.d3dDevice || item.type == LaunchItemType::Placeholder || item.type == LaunchItemType::Title) {
        return;
    }

    const std::string key = iconCacheKey(item);
    if (gSession.iconCache.textures.contains(key) || gSession.iconCache.pendingRequestKeys.contains(key)) {
        return;
    }
    if (auto failed = gSession.iconCache.failedRequestFrame.find(key); failed != gSession.iconCache.failedRequestFrame.end()) {
        if (ImGui::GetFrameCount() - failed->second < kFailedIconRetryFrames) {
            return;
        }
        gSession.iconCache.failedRequestFrame.erase(failed);
    }

    while (gSession.iconCache.pendingRequests.size() >= kMaxPendingIconRequests) {
        gSession.iconCache.pendingRequestKeys.erase(iconCacheKey(gSession.iconCache.pendingRequests.front()));
        gSession.iconCache.pendingRequests.pop_front();
    }
    gSession.iconCache.pendingRequests.push_back(item);
    gSession.iconCache.pendingRequestKeys.insert(key);
}

void processPendingIconRequests()
{
    if (gSession.useDefaultIcons) {
        if (!gSession.iconCache.pendingRequests.empty()) {
            gSession.iconCache.pendingRequests.clear();
            gSession.iconCache.pendingRequestKeys.clear();
        }
        maybeTrimIconWorkingSet();
        return;
    }
    if (gSession.iconCache.pendingRequests.empty()) {
        maybeTrimIconWorkingSet();
        return;
    }

    if (gSession.iconCache.loadBudgetThisFrame <= 0) {
        return;
    }
    while (!gSession.iconCache.pendingRequests.empty()) {
        LaunchItem item = std::move(gSession.iconCache.pendingRequests.front());
        gSession.iconCache.pendingRequests.pop_front();
        const std::string key = iconCacheKey(item);
        gSession.iconCache.pendingRequestKeys.erase(key);
        if (gSession.iconCache.textures.contains(key)) {
            continue;
        }
        if (gSession.iconCache.loadBudgetThisFrame <= 0) {
            requestLaunchIcon(item);
            return;
        }
        if (!iconTextureForItem(item)) {
            gSession.iconCache.failedRequestFrame[key] = ImGui::GetFrameCount();
        }
        if (gSession.iconCache.loadBudgetThisFrame <= 0) {
            return;
        }
    }
    maybeTrimIconWorkingSet();
}

void clearIconCacheForItem(const LaunchItem& item)
{
    eraseIconTextureCacheEntry(iconCacheKey(item));
}

std::int64_t nowUnix()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string timeText(std::int64_t value)
{
    if (value <= 0) {
        return "-";
    }
    std::time_t time = static_cast<std::time_t>(value);
    std::tm local{};
    localtime_s(&local, &time);
    char buffer[32]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
    return buffer;
}

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

LaunchItem withSearchVariables(const LaunchItem& item, const std::string& searchText)
{
    LaunchItem result = item;
    const std::string encoded = urlEncode(searchText);
    result.target = replaceAll(replaceAll(result.target.string(), "%so%", searchText), "%so-url%", encoded);
    result.startDirectory = replaceAll(replaceAll(result.startDirectory.string(), "%so%", searchText), "%so-url%", encoded);
    result.arguments = replaceAll(replaceAll(result.arguments, "%so%", searchText), "%so-url%", encoded);
    result.icon = replaceAll(replaceAll(result.icon, "%so%", searchText), "%so-url%", encoded);
    return result;
}

std::string effectiveParamId(const InteractiveParam& param, int index)
{
    if (!param.id.empty()) {
        return param.id;
    }
    return "param" + std::to_string(index + 1);
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

std::string asciiLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool startsWithIgnoreCase(const std::string& value, const std::string& prefix)
{
    if (prefix.empty()) {
        return false;
    }
    if (value.size() < prefix.size()) {
        return false;
    }
    return asciiLower(value.substr(0, prefix.size())) == asciiLower(prefix);
}

std::string interactiveParamKey(const std::string& itemId, const InteractiveParam& param, int index)
{
    return itemId + ":" + effectiveParamId(param, index);
}

std::vector<InteractiveHistoryCandidate> interactiveHistoryCandidates(const InteractiveParam& param, const std::string& input)
{
    std::vector<InteractiveHistoryCandidate> candidates;
    candidates.reserve(param.history.size());
    for (int i = 0; i < static_cast<int>(param.history.size()); ++i) {
        const InteractiveParamHistory& history = param.history[static_cast<size_t>(i)];
        if (history.value.empty()) {
            continue;
        }
        InteractiveHistoryCandidate candidate;
        candidate.value = history.value;
        candidate.useCount = history.useCount;
        candidate.lastUsedAt = history.lastUsedAt;
        candidate.sourceIndex = i;
        candidate.prefixMatch = startsWithIgnoreCase(history.value, input);
        candidates.push_back(std::move(candidate));
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const InteractiveHistoryCandidate& a, const InteractiveHistoryCandidate& b) {
        if (a.useCount != b.useCount) return a.useCount > b.useCount;
        if (a.lastUsedAt != b.lastUsedAt) return a.lastUsedAt > b.lastUsedAt;
        return a.value < b.value;
    });
    if (candidates.size() > static_cast<size_t>(kMaxInteractiveHistorySuggestions)) {
        candidates.resize(static_cast<size_t>(kMaxInteractiveHistorySuggestions));
    }
    return candidates;
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
    if (param.history.size() > kMaxInteractiveParamHistory) {
        param.history.resize(kMaxInteractiveParamHistory);
    }
}

void recordInteractiveHistory(LaunchItem& item, const std::vector<std::string>& values)
{
    const std::int64_t timestamp = nowUnix();
    for (int i = 0; i < static_cast<int>(item.interactiveParams.size()); ++i) {
        if (i >= static_cast<int>(values.size())) {
            break;
        }
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

void openInteractiveRunPrompt(const LaunchItem& item, int showCommand, const std::string& searchText)
{
    gSession.interactiveRunItem = item;
    gSession.interactiveRunItemId = item.id;
    gSession.interactiveRunSearchText = searchText;
    gSession.interactiveRunShowCommand = showCommand;
    gSession.interactiveRunValues.clear();
    gSession.interactiveRunValues.reserve(item.interactiveParams.size());
    gSession.interactiveHistoryParamKey.clear();
    gSession.interactiveHistorySelected = -1;
    for (const InteractiveParam& param : item.interactiveParams) {
        gSession.interactiveRunValues.push_back(defaultParamValue(param));
    }
    gSession.showInteractiveRun = true;
    gSession.openInteractiveRunPopup = true;
}

std::string replaceInteractiveValue(std::string value, const std::string& id, const std::string& replacement)
{
    value = replaceAll(value, "{{" + id + "}}", replacement);
    value = replaceAll(value, "%" + id + "%", replacement);
    return value;
}

LaunchItem withInteractiveValues(const LaunchItem& item, const std::vector<std::string>& values)
{
    LaunchItem result = item;
    for (int i = 0; i < static_cast<int>(item.interactiveParams.size()); ++i) {
        const std::string id = effectiveParamId(item.interactiveParams[static_cast<size_t>(i)], i);
        const std::string value = i < static_cast<int>(values.size()) ? values[static_cast<size_t>(i)]
                                                                      : defaultParamValue(item.interactiveParams[static_cast<size_t>(i)]);
        result.target = replaceInteractiveValue(result.target.string(), id, value);
        result.startDirectory = replaceInteractiveValue(result.startDirectory.string(), id, value);
        result.arguments = replaceInteractiveValue(result.arguments, id, value);
        result.icon = replaceInteractiveValue(result.icon, id, value);
    }
    return result;
}

Category* selectedCategory(AppContext& context)
{
    if (context.runtime().selectedCategory < 0 ||
        context.runtime().selectedCategory >= static_cast<int>(context.persisted().categories.size())) {
        return nullptr;
    }
    return &context.persisted().categories[context.runtime().selectedCategory];
}

std::unique_ptr<IconTexture> createTextureFromImageFile(const std::filesystem::path& path)
{
    if (!gSession.d3dDevice || path.empty()) {
        return {};
    }

    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf())))) {
        return {};
    }
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(path.wstring().c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad,
                                                  decoder.GetAddressOf()))) {
        return {};
    }
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) {
        return {};
    }
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf()))) {
        return {};
    }
    if (FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom))) {
        return {};
    }
    UINT width = 0;
    UINT height = 0;
    converter->GetSize(&width, &height);
    if (width == 0 || height == 0) {
        return {};
    }
    std::vector<std::uint8_t> pixels(static_cast<size_t>(width) * height * 4);
    if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data()))) {
        return {};
    }

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA data{};
    data.pSysMem = pixels.data();
    data.SysMemPitch = width * 4;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    if (FAILED(gSession.d3dDevice->CreateTexture2D(&desc, &data, texture.GetAddressOf()))) {
        return {};
    }

    auto result = std::make_unique<IconTexture>();
    if (FAILED(gSession.d3dDevice->CreateShaderResourceView(texture.Get(), nullptr, result->srv.GetAddressOf()))) {
        return {};
    }
    result->width = static_cast<int>(width);
    result->height = static_cast<int>(height);
    return result;
}

bool hasThemeBackground(const ThemeDefinition& theme)
{
    return theme.background.enabled && !theme.background.imagePath.empty();
}

void resetBackgroundTextureCache()
{
    gSession.backgroundTexture.key.clear();
    gSession.backgroundTexture.frames.clear();
    gSession.backgroundTexture.texture.reset();
    gSession.backgroundTexture.frameIndex = -1;
}

bool mainWindowHasForeground()
{
    HWND hwnd = mainWindowHandle();
    return hwnd && GetForegroundWindow() == hwnd;
}

std::filesystem::path animatedBackgroundCacheRoot(const AppContext& context)
{
    return context.config.directory() / "background-cache";
}

std::unique_ptr<IconTexture> loadBackgroundTexture(const std::filesystem::path& path)
{
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return {};
    }
    try {
        return createTextureFromImageFile(path);
    } catch (...) {
        return {};
    }
}

IconTexture* animatedBackgroundTextureForTheme(const AppContext& context, const ThemeDefinition& theme)
{
    const int fps = std::clamp(theme.background.animationFps, 1, 30);
    const int maxWidth = std::clamp(theme.background.animationMaxWidth, 240, 3840);
    const int quality = std::clamp(theme.background.animationQuality, 2, 31);
    const std::string cacheKey = animatedBackgroundCacheKey(theme.background.imagePath, fps, maxWidth, quality);
    const std::string key = std::string("animated:") + cacheKey;
    if (gSession.backgroundTexture.key != key) {
        resetBackgroundTextureCache();
        gSession.backgroundTexture.key = key;
        gSession.backgroundTexture.frames =
            animatedBackgroundFrames(animatedBackgroundCacheDirectory(animatedBackgroundCacheRoot(context), cacheKey));
    }
    if (gSession.backgroundTexture.frames.empty()) {
        return nullptr;
    }

    const auto frameCount = static_cast<int>(gSession.backgroundTexture.frames.size());
    const int frame = mainWindowHasForeground() || gSession.backgroundTexture.frameIndex < 0
                          ? static_cast<int>(ImGui::GetTime() * static_cast<double>(fps)) % frameCount
                          : gSession.backgroundTexture.frameIndex;
    if (gSession.backgroundTexture.frameIndex != frame) {
        gSession.backgroundTexture.texture = loadBackgroundTexture(gSession.backgroundTexture.frames[static_cast<size_t>(frame)]);
        gSession.backgroundTexture.frameIndex = frame;
    }

    return gSession.backgroundTexture.texture.get();
}

IconTexture* backgroundTextureForTheme(const AppContext& context, const ThemeDefinition& theme)
{
    if (!hasThemeBackground(theme) || !gSession.d3dDevice) {
        return nullptr;
    }
    if (theme.background.animated) {
        return animatedBackgroundTextureForTheme(context, theme);
    }

    const std::string key = std::string("static:") + theme.background.imagePath.string();
    if (gSession.backgroundTexture.key != key) {
        resetBackgroundTextureCache();
        gSession.backgroundTexture.key = key;
        gSession.backgroundTexture.texture = loadBackgroundTexture(theme.background.imagePath);
    }
    return gSession.backgroundTexture.texture.get();
}

bool removeItemFromList(std::vector<LaunchItem>& items, const std::unordered_set<std::string>& pending)
{
    bool changed = false;
    for (LaunchItem& item : items) {
        if (removeItemFromList(item.children, pending)) {
            changed = true;
        }
    }
    const auto oldSize = items.size();
    items.erase(std::remove_if(items.begin(), items.end(),
                               [&](const LaunchItem& item) {
                                   return pending.contains(item.id);
                               }),
                items.end());
    return changed || items.size() != oldSize;
}

LaunchItem* findItemById(AppContext& context, const std::string& id)
{
    for (Category& category : context.persisted().categories) {
        if (LaunchItem* item = findItemInList(category.items, id)) {
            return item;
        }
    }
    return nullptr;
}

const LaunchItem* findItemById(const AppContext& context, const std::string& id)
{
    for (const Category& category : context.persisted().categories) {
        if (const LaunchItem* item = findItemInList(category.items, id)) {
            return item;
        }
    }
    return nullptr;
}

std::vector<LaunchItem>* currentItems(AppContext& context)
{
    Category* category = selectedCategory(context);
    if (!category) {
        return nullptr;
    }
    std::vector<LaunchItem>* items = &category->items;
    std::vector<std::string> validStack;
    for (const std::string& id : context.runtime().currentFolderStack) {
        LaunchItem* folder = findItemInList(*items, id);
        if (!folder || folder->type != LaunchItemType::VirtualFolder) {
            break;
        }
        validStack.push_back(id);
        items = &folder->children;
    }
    if (validStack.size() != context.runtime().currentFolderStack.size()) {
        context.runtime().currentFolderStack = std::move(validStack);
    }
    return items;
}

const std::vector<LaunchItem>* currentItems(const AppContext& context)
{
    auto& mutableContext = const_cast<AppContext&>(context);
    return currentItems(mutableContext);
}

std::vector<LaunchItem>* itemsForFolderStack(AppContext& context, const std::vector<std::string>& stack)
{
    Category* category = selectedCategory(context);
    if (!category) {
        return nullptr;
    }
    std::vector<LaunchItem>* items = &category->items;
    for (const std::string& id : stack) {
        LaunchItem* folder = findItemInList(*items, id);
        if (!folder || folder->type != LaunchItemType::VirtualFolder) {
            return nullptr;
        }
        items = &folder->children;
    }
    return items;
}

std::string dragPayloadId(const ImGuiPayload* payload)
{
    if (!payload || payload->DataSize <= 1) {
        return {};
    }
    return std::string(static_cast<const char*>(payload->Data), static_cast<size_t>(payload->DataSize - 1));
}

std::string stackKey(const std::vector<std::string>& stack)
{
    std::string key = "folder:";
    for (const std::string& id : stack) {
        key += id;
        key += "/";
    }
    return key;
}

ImVec4 withAlpha(ImU32 color, float alphaMultiplier)
{
    ImVec4 result = ImGui::ColorConvertU32ToFloat4(color);
    result.w = std::clamp(result.w * alphaMultiplier, 0.0f, 1.0f);
    return result;
}

std::vector<LaunchItem>* editingItems(AppContext& context)
{
    if (gSession.editingFolderId.empty()) {
        if (gSession.editingCategory < 0 || gSession.editingCategory >= static_cast<int>(context.persisted().categories.size())) {
            return nullptr;
        }
        return &context.persisted().categories[gSession.editingCategory].items;
    }
    for (Category& category : context.persisted().categories) {
        if (LaunchItem* folder = findItemInList(category.items, gSession.editingFolderId)) {
            return &folder->children;
        }
    }
    return nullptr;
}

int dropInsertIndex(int targetIndex, const ImRect& rect)
{
    const bool after = ImGui::GetMousePos().y > (rect.Min.y + rect.Max.y) * 0.5f;
    return targetIndex + (after ? 1 : 0);
}

void reorderCategory(AppContext& context, int from, int insertAt)
{
    if (model_actions::reorderCategory(context.persisted(), context.runtime(), from, insertAt)) {
        context.commitContentChange();
    }
}

void reorderItem(AppContext& context, std::vector<LaunchItem>& items, int from, int insertAt)
{
    if (model_actions::reorderItem(items, context.persisted().settings, currentListLayoutLocked(context), from, insertAt)) {
        context.commitContentChange();
    }
}

std::string makeId(const std::string& name, int index)
{
    return name + "-" + std::to_string(index + 1) + "-" + std::to_string(nowUnix());
}

LaunchItem makeNewItem(const std::string& name, LaunchItemType type, int index)
{
    LaunchItem item;
    item.id = makeId("item", index);
    item.name = name;
    item.type = type;
    item.createdAt = nowUnix();
    item.lastEditedAt = item.createdAt;
    if (type == LaunchItemType::Url) {
        item.target = "https://";
        item.subtitle = "Url";
        item.remark = "Url";
    } else if (type == LaunchItemType::Script) {
        item.subtitle = "Script";
        item.remark = "Script";
    } else if (type == LaunchItemType::VirtualFolder) {
        item.subtitle = "Virtual folder";
        item.remark = "Virtual folder";
    } else if (type == LaunchItemType::Note) {
        item.subtitle = tr("Notes");
        item.remark = tr("Notes");
        item.fallbackColor = "#6A9A7CFF";
    }
    return item;
}

void openItemEditor(AppContext& context, int itemIndex, LaunchItemType type = LaunchItemType::App)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }

    gSession.editingCategory = context.runtime().selectedCategory;
    gSession.editingFolderId = context.runtime().currentFolderStack.empty() ? "" : context.runtime().currentFolderStack.back();
    gSession.editingItem = itemIndex;
    if (itemIndex >= 0 && itemIndex < static_cast<int>(items->size())) {
        gSession.editingDraft = (*items)[itemIndex];
    } else {
        const char* defaultName = type == LaunchItemType::Title           ? "Title"
                                  : type == LaunchItemType::VirtualFolder ? "Virtual Folder"
                                  : type == LaunchItemType::Note          ? tr("Note")
                                                                          : "New Item";
        gSession.editingDraft = makeNewItem(defaultName, type, static_cast<int>(items->size()));
    }
    gSession.editingTarget = gSession.editingDraft.target.string();
    gSession.editingStartDir = gSession.editingDraft.startDirectory.string();
    gSession.editingRemark = gSession.editingDraft.remark.empty() ? gSession.editingDraft.subtitle : gSession.editingDraft.remark;
    gSession.editingIcon = gSession.editingDraft.icon;
    gSession.showItemEditor = true;
    gSession.openItemEditorPopup = true;
}

void openItemEditorWithDraft(AppContext& context, const LaunchItem& sourceItem)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }

    LaunchItem item = sourceItem;
    item.id = makeId("item", static_cast<int>(items->size()));
    item.createdAt = nowUnix();
    item.lastEditedAt = item.createdAt;
    gSession.editingCategory = context.runtime().selectedCategory;
    gSession.editingFolderId = context.runtime().currentFolderStack.empty() ? "" : context.runtime().currentFolderStack.back();
    gSession.editingItem = -1;
    gSession.editingDraft = std::move(item);
    gSession.editingTarget = gSession.editingDraft.target.string();
    gSession.editingStartDir = gSession.editingDraft.startDirectory.string();
    gSession.editingRemark = gSession.editingDraft.remark.empty() ? gSession.editingDraft.subtitle : gSession.editingDraft.remark;
    gSession.editingIcon = gSession.editingDraft.icon;
    gSession.showItemEditor = true;
    gSession.openItemEditorPopup = true;
}

void appendItem(AppContext& context, LaunchItemType type, const std::string& name)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }
    LaunchItem item = makeNewItem(name, type, static_cast<int>(items->size()));
    items->push_back(item);
    selectSingle(context, items->back());
    context.commitContentChange();
}

void appendNoteItem(AppContext& context)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }

    Note& note = context.notes.createNote(tr("Untitled Note"));
    LaunchItem item = makeNewItem(NotesStore::displayTitle(note), LaunchItemType::Note, static_cast<int>(items->size()));
    item.id = "note-item-" + note.id + "-" + std::to_string(nowUnix());
    item.target = note.id;
    item.subtitle = tr("Notes");
    item.remark = tr("Notes");
    items->push_back(std::move(item));
    selectSingle(context, items->back());
    context.runtime().selectedNoteId = note.id;
    context.runtime().showNotes = true;
    context.runtime().showNoteQuick = false;
    context.commitContentChange();
}

bool reorderItemById(AppContext& context, std::vector<LaunchItem>& items, const std::string& id, int insertAt)
{
    const int from = itemIndexById(items, id);
    if (from < 0) {
        return false;
    }
    reorderItem(context, items, from, insertAt);
    return true;
}

bool moveItemByIdToList(AppContext& context, const std::string& id, std::vector<LaunchItem>& destination, int insertAt = -1)
{
    const model_actions::MoveItemsResult result = model_actions::moveItemByIdToList(context.persisted(), id, destination, insertAt);
    if (!result.changed) {
        return false;
    }
    if (result.movedAcrossLists) {
        clearSelection(context);
    }
    context.commitContentChange();
    return true;
}

bool moveItemIdsToList(AppContext& context, const std::vector<std::string>& ids, std::vector<LaunchItem>& destination, int insertAt = -1)
{
    const model_actions::MoveItemsResult result = model_actions::moveItemIdsToList(context.persisted(), ids, destination, insertAt);
    if (!result.changed) {
        return false;
    }
    if (result.movedIds.size() > 1) {
        const std::string activeId =
            std::find(result.movedIds.begin(), result.movedIds.end(), context.runtime().selectedItemId) != result.movedIds.end()
                ? context.runtime().selectedItemId
                : result.movedIds.front();
        selectIds(context, result.movedIds, activeId);
    } else if (result.movedAcrossLists) {
        clearSelection(context);
    }
    context.commitContentChange();
    return true;
}

bool isFolderHoveredForAutoEnter(const LaunchItem& item, const ImVec2& tile, float tileW, float tileH)
{
    if (item.type != LaunchItemType::VirtualFolder) {
        return false;
    }
    const ImRect centerZone(ImVec2(tile.x + tileW * 0.18f, tile.y + tileH * 0.14f), ImVec2(tile.x + tileW * 0.82f, tile.y + tileH * 0.86f));
    return centerZone.Contains(ImGui::GetIO().MousePos);
}

void requestHideMainWindow()
{
    if (HWND hwnd = mainWindowHandle()) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
}

void resetSearchState(AppContext& context)
{
    gSession.focusSearch = false;
    gSession.searchOpen = false;
    gSession.searchSubmit = false;
    gSession.searchSelected = 0;
    gSession.searchMove = 0;
    gSession.searchPageMove = 0;
    gSession.searchEditedAt = 0.0;
    gSession.searchQueryText.clear();
    gSession.searchResultsCache.results.clear();
    gSession.searchResultsCache.query.clear();
    gSession.searchResultsCache.settingsKey.clear();
    gSession.searchResultsCache.indexRevision = 0;
    gSession.searchResultsCache.valid = false;
    context.runtime().searchText.clear();
}

bool hasOpenManagedWindow(const AppContext& context)
{
    return context.runtime().showSettings || context.runtime().showThemeEditor || gSession.showItemEditor || gSession.showTaskPlanner ||
           gSession.showBuildInfo || gSession.editingCategoryIndex >= 0 || hasPendingDeleteState() ||
           ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId);
}

void snapMainWindowIfNeeded(const AppSettings& settings)
{
    if ((!settings.magneticScreenCorner && !settings.magneticScreenEdge) || settings.lockWindowPosition) {
        return;
    }
    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        return;
    }
    HWND hwnd = mainWindowHandle();
    if (!hwnd || !IsWindowVisible(hwnd)) {
        return;
    }
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        return;
    }
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    if (!GetMonitorInfoW(monitor, &info)) {
        return;
    }
    constexpr int threshold = 36;
    int x = rect.left;
    int y = rect.top;
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    bool changed = false;

    const bool rectNearLeft = rect.left <= info.rcWork.left + threshold;
    const bool rectNearRight = rect.right >= info.rcWork.right - threshold;
    const bool rectNearTop = rect.top <= info.rcWork.top + threshold;
    const bool rectNearBottom = rect.bottom >= info.rcWork.bottom - threshold;
    if (settings.magneticScreenCorner && ((rectNearLeft || rectNearRight) && (rectNearTop || rectNearBottom))) {
        if (rectNearLeft) {
            x = info.rcWork.left;
            changed = true;
        }
        if (rectNearRight) {
            x = info.rcWork.right - width;
            changed = true;
        }
        if (rectNearTop) {
            y = info.rcWork.top;
            changed = true;
        }
        if (rectNearBottom) {
            y = info.rcWork.bottom - height;
            changed = true;
        }
    } else if (settings.magneticScreenEdge) {
        if (rectNearLeft) {
            x = info.rcWork.left;
            changed = true;
        } else if (rectNearRight) {
            x = info.rcWork.right - width;
            changed = true;
        }
        if (rectNearTop) {
            y = info.rcWork.top;
            changed = true;
        } else if (rectNearBottom) {
            y = info.rcWork.bottom - height;
            changed = true;
        }
    }
    if (changed) {
        SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void appendPlaceholders(AppContext& context, int count)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }
    for (int i = 0; i < count; ++i) {
        LaunchItem item = makeNewItem(" ", LaunchItemType::Placeholder, static_cast<int>(items->size()));
        items->push_back(item);
        selectSingle(context, items->back());
    }
    context.commitContentChange();
}

bool removeItemById(AppContext& context, const std::string& id)
{
    for (Category& category : context.persisted().categories) {
        std::unordered_set<std::string> ids{id};
        if (removeItemFromList(category.items, ids)) {
            return true;
        }
    }
    return false;
}

std::string itemTypeText(LaunchItemType type)
{
    switch (type) {
    case LaunchItemType::App: return "App";
    case LaunchItemType::Url: return "Url";
    case LaunchItemType::Script: return "Script";
    case LaunchItemType::BuiltIn: return "BuiltIn";
    case LaunchItemType::Placeholder: return "Placeholder";
    case LaunchItemType::Title: return "Title";
    case LaunchItemType::VirtualFolder: return "VirtualFolder";
    case LaunchItemType::Note: return "Note";
    }
    return "App";
}

std::string itemPropertiesText(const LaunchItem& item)
{
    std::string text;
    text += tr("Name: ") + item.name + "\n";
    text += tr("Type: ") + itemTypeText(item.type) + "\n";
    text += tr("Target: ") + item.target.string() + "\n";
    text += tr("Start directory: ") + item.startDirectory.string() + "\n";
    text += tr("Arguments: ") + item.arguments + "\n";
    text += tr("Icon: ") + item.icon + "\n";
    text += tr("Search keywords: ") + item.keywords + "\n";
    text += tr("Hotkey: ") + item.hotkey + "\n";
    text += tr("Remark: ") + item.remark + "\n";
    text += tr("Run count: ") + std::to_string(item.runCount);
    return text;
}

void enterVirtualFolder(AppContext& context, const LaunchItem& item);
void openNoteById(AppContext& context, const std::string& id);
void openNoteEditorById(AppContext& context, const std::string& id);
bool launchResolvedItem(AppContext& context, LaunchItem& sourceItem, LaunchItem launchItem, int showCommand);

void runItemsInList(AppContext& context, std::vector<LaunchItem>& items)
{
    for (LaunchItem& child : items) {
        if (child.type == LaunchItemType::VirtualFolder) {
            if (context.persisted().settings.virtualFolderRunsAll) {
                runItemsInList(context, child.children);
            }
            continue;
        }
        if (child.type == LaunchItemType::Title || child.type == LaunchItemType::Placeholder) {
            continue;
        }
        if (child.type == LaunchItemType::Note) {
            openNoteById(context, child.target.string());
            ++child.runCount;
            child.lastRunAt = nowUnix();
            continue;
        }
        LaunchItem launchItem =
            context.persisted().settings.searchParamVariable ? withSearchVariables(child, context.runtime().searchText) : child;
        launchResolvedItem(context, child, std::move(launchItem), SW_SHOWNORMAL);
    }
    context.save();
}

bool launchResolvedItem(AppContext& context, LaunchItem& sourceItem, LaunchItem launchItem, int showCommand)
{
    if (context.launcher.launch(launchItem, showCommand)) {
        ++sourceItem.runCount;
        sourceItem.lastRunAt = nowUnix();
        context.save();
        if (context.persisted().settings.closeSearchAfterRun) {
            resetSearchState(context);
        }
        if (context.persisted().settings.runItemHidesMain || context.persisted().settings.hideAfterRun) {
            requestHideMainWindow();
        }
        return true;
    }
    return false;
}

void runItem(AppContext& context, LaunchItem& item, int showCommand = SW_SHOWNORMAL)
{
    if (item.type == LaunchItemType::VirtualFolder) {
        enterVirtualFolder(context, item);
        return;
    }
    if (item.type == LaunchItemType::Title || item.type == LaunchItemType::Placeholder) {
        return;
    }
    if (item.type == LaunchItemType::Note) {
        openNoteById(context, item.target.string());
        ++item.runCount;
        item.lastRunAt = nowUnix();
        context.save();
        if (context.persisted().settings.closeSearchAfterRun) {
            resetSearchState(context);
        }
        return;
    }
    if (itemNeedsInteractivePrompt(item)) {
        const std::string searchText = context.persisted().settings.searchParamVariable ? context.runtime().searchText : std::string{};
        openInteractiveRunPrompt(item, showCommand, searchText);
        return;
    }
    LaunchItem launchItem =
        context.persisted().settings.searchParamVariable ? withSearchVariables(item, context.runtime().searchText) : item;
    launchResolvedItem(context, item, std::move(launchItem), showCommand);
}

void appendBuiltInItem(AppContext& context, const std::string& name, const std::string& target, const std::string& args = {})
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }
    LaunchItem item = makeNewItem(name, LaunchItemType::BuiltIn, static_cast<int>(items->size()));
    item.target = target;
    item.arguments = args;
    item.subtitle = "Built-in";
    item.remark = item.subtitle;
    items->push_back(item);
    selectSingle(context, items->back());
    context.commitContentChange();
}

ImU32 iconColor(const LaunchItem& item)
{
    auto hexDigit = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    };
    auto hexByte = [&](const std::string& text, int offset, int fallback) -> int {
        if (offset + 1 >= static_cast<int>(text.size())) return fallback;
        const int hi = hexDigit(text[offset]);
        const int lo = hexDigit(text[offset + 1]);
        return hi < 0 || lo < 0 ? fallback : (hi << 4) | lo;
    };
    if (!item.fallbackColor.empty()) {
        std::string color = item.fallbackColor;
        if (color[0] == '#') {
            color.erase(color.begin());
        }
        if (color.size() == 6 || color.size() == 8) {
            const int r = hexByte(color, 0, 140);
            const int g = hexByte(color, 2, 140);
            const int b = hexByte(color, 4, 140);
            const int a = color.size() == 8 ? hexByte(color, 6, 255) : 255;
            return IM_COL32(r, g, b, a);
        }
    }
    if (item.type == LaunchItemType::Title) {
        return IM_COL32(150, 150, 150, 255);
    }
    if (item.type == LaunchItemType::Placeholder) {
        return IM_COL32(205, 205, 205, 255);
    }
    if (item.type == LaunchItemType::Note) {
        return IM_COL32(106, 154, 124, 255);
    }
    static constexpr std::array<ImU32, 8> palette = {IM_COL32(72, 136, 199, 255), IM_COL32(244, 119, 63, 255), IM_COL32(83, 174, 95, 255),
                                                     IM_COL32(94, 126, 220, 255), IM_COL32(233, 76, 96, 255),  IM_COL32(66, 165, 176, 255),
                                                     IM_COL32(244, 172, 50, 255), IM_COL32(140, 112, 206, 255)};
    return palette[std::hash<std::string>{}(item.id) % palette.size()];
}

void drawLaunchIconOnList(ImDrawList* dl, const LaunchItem& item, const ImVec2& pos, float size)
{
    const float rounding = item.type == LaunchItemType::Url ? size * 0.5f : 8.0f;
    if (item.type == LaunchItemType::VirtualFolder) {
        const float glyphFontSize = size * 0.82f;
        const ImVec2 glyphSize = ImGui::GetFont()->CalcTextSizeA(glyphFontSize, FLT_MAX, 0.0f, Icons::Folder);
        dl->AddText(nullptr, glyphFontSize, ImVec2(pos.x + (size - glyphSize.x) * 0.5f, pos.y + (size - glyphSize.y) * 0.5f),
                    iconColor(item), Icons::Folder);
        return;
    }
    if (item.type == LaunchItemType::Note) {
        dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), iconColor(item), rounding);
        const float glyphFontSize = size * 0.62f;
        const ImVec2 glyphSize = ImGui::GetFont()->CalcTextSizeA(glyphFontSize, FLT_MAX, 0.0f, Icons::Note);
        dl->AddText(nullptr, glyphFontSize, ImVec2(pos.x + (size - glyphSize.x) * 0.5f, pos.y + (size - glyphSize.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), Icons::Note);
        return;
    }
    if (IconTexture* texture = iconTextureForItem(item); texture && texture->srv) {
        dl->AddImageRounded(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->srv.Get())), pos,
                            ImVec2(pos.x + size, pos.y + size), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, rounding);
        return;
    }

    dl->AddRectFilled(pos, ImVec2(pos.x + size, pos.y + size), iconColor(item), rounding);
    if (item.type == LaunchItemType::Placeholder) {
        return;
    }
    std::string label = item.name.empty() ? "?" : item.name.substr(0, 1);
    const float fontSize = std::max(12.0f, size * 0.58f);
    const ImVec2 labelSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str());
    dl->AddText(nullptr, fontSize, ImVec2(pos.x + (size - labelSize.x) * 0.5f, pos.y + (size - labelSize.y) * 0.5f),
                IM_COL32(255, 255, 255, 255), label.c_str());
}

void drawLaunchIcon(const LaunchItem& item, const ImVec2& pos, float size)
{
    drawLaunchIconOnList(ImGui::GetWindowDrawList(), item, pos, size);
}

void drawConfiguredBackground(const AppContext& context, const ThemeDefinition& theme, const ImVec2& origin, const ImVec2& size)
{
    IconTexture* texture = backgroundTextureForTheme(context, theme);
    if (!texture || !texture->srv || texture->width <= 0 || texture->height <= 0) {
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImTextureID id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->srv.Get()));
    const int alpha = std::clamp(theme.background.opacity * 255 / 100, 0, 255);
    const ImU32 tint = IM_COL32(255, 255, 255, alpha);
    const float imageW = static_cast<float>(texture->width);
    const float imageH = static_cast<float>(texture->height);
    const float areaW = std::max(1.0f, size.x);
    const float areaH = std::max(1.0f, size.y);

    ImVec2 min = origin;
    ImVec2 max(origin.x + areaW, origin.y + areaH);
    if (theme.background.imageMode == 3) {
        dl->PushClipRect(min, max, true);
        for (float y = min.y; y < max.y; y += imageH) {
            for (float x = min.x; x < max.x; x += imageW) {
                dl->AddImage(id, ImVec2(x, y), ImVec2(x + imageW, y + imageH), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint);
            }
        }
        dl->PopClipRect();
        return;
    }

    float drawW = imageW;
    float drawH = imageH;
    if (theme.background.imageMode == 0 || theme.background.imageMode == 1) {
        const float scale =
            theme.background.imageMode == 0 ? std::max(areaW / imageW, areaH / imageH) : std::min(areaW / imageW, areaH / imageH);
        drawW = imageW * scale;
        drawH = imageH * scale;
    } else if (theme.background.imageMode == 2) {
        drawW = areaW;
        drawH = areaH;
    }
    min = ImVec2(origin.x + (areaW - drawW) * 0.5f, origin.y + (areaH - drawH) * 0.5f);
    max = ImVec2(min.x + drawW, min.y + drawH);
    dl->PushClipRect(origin, ImVec2(origin.x + areaW, origin.y + areaH), true);
    dl->AddImageRounded(id, min, max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint, uiPalette(theme).windowRounding);
    dl->PopClipRect();
}

bool mainViewportIsForeground()
{
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    if (!viewport) {
        return false;
    }
    auto* mainHwnd = static_cast<HWND>(viewport->PlatformHandleRaw);
    return mainHwnd && GetForegroundWindow() == mainHwnd;
}

bool mouseOverMainWindow()
{
    HWND main = mainWindowHandle();
    if (!main) {
        return false;
    }
    POINT pt{};
    GetCursorPos(&pt);
    HWND hit = WindowFromPoint(pt);
    if (!hit) {
        return false;
    }
    HWND root = GetAncestor(hit, GA_ROOT);
    return root == main || hit == main;
}

void selectAllCurrentCategory(AppContext& context);

void selectAllCurrentCategory(AppContext& context)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items) {
        return;
    }
    std::vector<std::string> ids;
    ids.reserve(items->size());
    for (const LaunchItem& item : *items) {
        ids.push_back(item.id);
    }
    if (!items->empty()) {
        selectIds(context, ids, items->back().id);
    } else {
        clearSelection(context);
    }
}

std::vector<int> orderedItemIndicesForNavigation(const std::vector<LaunchItem>& items, SortMode mode)
{
    std::vector<int> indices(items.size());
    for (int i = 0; i < static_cast<int>(indices.size()); ++i) {
        indices[i] = i;
    }
    if (mode == SortMode::Free) {
        return indices;
    }

    std::stable_sort(indices.begin(), indices.end(), [&](int lhs, int rhs) {
        const LaunchItem& a = items[lhs];
        const LaunchItem& b = items[rhs];
        switch (mode) {
        case SortMode::Name: return a.name < b.name;
        case SortMode::Type: return static_cast<int>(a.type) < static_cast<int>(b.type);
        case SortMode::RunCount: return a.runCount > b.runCount;
        case SortMode::CreatedAt: return a.createdAt > b.createdAt;
        case SortMode::LastRunAt: return a.lastRunAt > b.lastRunAt;
        case SortMode::LastEditedAt: return a.lastEditedAt > b.lastEditedAt;
        case SortMode::Free:
        default: return lhs < rhs;
        }
    });
    return indices;
}

ItemViewMode currentNavigationViewMode(const AppContext& context)
{
    const RuntimeState& runtime = context.runtime();
    const PersistedState& persisted = context.persisted();
    if (runtime.selectedCategory >= 0 && runtime.selectedCategory < static_cast<int>(persisted.categories.size())) {
        const Category& category = persisted.categories[runtime.selectedCategory];
        if (!category.useGlobalLayout) {
            return category.viewMode;
        }
    }
    return persisted.settings.viewMode;
}

int currentNavigationIconSize(const AppContext& context)
{
    const RuntimeState& runtime = context.runtime();
    const PersistedState& persisted = context.persisted();
    if (runtime.selectedCategory >= 0 && runtime.selectedCategory < static_cast<int>(persisted.categories.size())) {
        const Category& category = persisted.categories[runtime.selectedCategory];
        if (!category.useGlobalLayout) {
            return category.iconSize;
        }
    }
    return persisted.settings.iconSize;
}

struct ItemNavigationEntry {
    int itemIndex = -1;
    int row = 0;
    int column = 0;
};

std::vector<ItemNavigationEntry> itemNavigationEntries(const AppContext& context, const std::vector<LaunchItem>& items)
{
    const AppSettings& settings = context.persisted().settings;
    const ItemViewMode viewMode = currentNavigationViewMode(context);
    const std::vector<int> order = orderedItemIndicesForNavigation(items, settings.sortMode);
    std::vector<ItemNavigationEntry> entries;
    entries.reserve(order.size());
    if (viewMode == ItemViewMode::List) {
        for (int row = 0; row < static_cast<int>(order.size()); ++row) {
            entries.push_back({order[row], row, 0});
        }
        return entries;
    }

    const float iconSize = static_cast<float>(std::clamp(currentNavigationIconSize(context), 24, 96));
    const bool tileMode = viewMode == ItemViewMode::Tile;
    const float tileW = tileMode ? std::max(120.0f, iconSize + 92.0f) : std::clamp(iconSize + 40.0f, 72.0f, 128.0f);
    const float gapX = iconSize <= 40.0f ? 6.0f : iconSize >= 56.0f ? 18.0f : 12.0f;
    const float contentWidth = std::max(0.0f, ImGui::GetIO().DisplaySize.x - kRailWidth);
    const float availableWidth = std::max(0.0f, contentWidth - 44.0f);
    const int columns = std::max(1, static_cast<int>(availableWidth / (tileW + gapX)));

    int row = 0;
    int column = 0;
    for (int itemIndex : order) {
        const LaunchItem& item = items[itemIndex];
        if (item.type == LaunchItemType::Title) {
            if (column != 0) {
                ++row;
                column = 0;
            }
            entries.push_back({itemIndex, row, 0});
            ++row;
            continue;
        }
        entries.push_back({itemIndex, row, column});
        ++column;
        if (column >= columns) {
            column = 0;
            ++row;
        }
    }
    return entries;
}

void switchSelectedCategory(AppContext& context, int delta)
{
    std::vector<Category>& categories = context.persisted().categories;
    if (categories.empty()) {
        return;
    }
    const int count = static_cast<int>(categories.size());
    const int current = std::clamp(context.runtime().selectedCategory, 0, count - 1);
    context.runtime().selectedCategory = (current + delta + count) % count;
    context.runtime().currentFolderStack.clear();
    clearSelection(context);
    gSession.resetMainDockScroll = true;
}

bool moveCurrentItemSelection(AppContext& context, ImGuiKey key)
{
    std::vector<LaunchItem>* items = currentItems(context);
    if (!items || items->empty()) {
        clearSelection(context);
        return false;
    }

    const std::vector<ItemNavigationEntry> entries = itemNavigationEntries(context, *items);
    if (entries.empty()) {
        clearSelection(context);
        return false;
    }

    int currentEntry = -1;
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        if ((*items)[entries[i].itemIndex].id == context.runtime().selectedItemId) {
            currentEntry = i;
            break;
        }
    }
    if (currentEntry < 0) {
        const bool reverse = key == ImGuiKey_UpArrow || key == ImGuiKey_LeftArrow;
        const int target = reverse ? static_cast<int>(entries.size()) - 1 : 0;
        selectSingle(context, (*items)[entries[target].itemIndex]);
        return true;
    }

    const ItemNavigationEntry& current = entries[currentEntry];
    int targetEntry = -1;
    auto chooseInRow = [&](int row, int preferredColumn) {
        int best = -1;
        int bestDistance = std::numeric_limits<int>::max();
        for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
            if (entries[i].row != row) {
                continue;
            }
            const int distance = std::abs(entries[i].column - preferredColumn);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = i;
            }
        }
        return best;
    };

    if (currentNavigationViewMode(context) == ItemViewMode::List) {
        if (key == ImGuiKey_UpArrow && currentEntry > 0) {
            targetEntry = currentEntry - 1;
        } else if (key == ImGuiKey_DownArrow && currentEntry + 1 < static_cast<int>(entries.size())) {
            targetEntry = currentEntry + 1;
        }
    } else if (key == ImGuiKey_LeftArrow) {
        for (int i = currentEntry - 1; i >= 0; --i) {
            if (entries[i].row != current.row) {
                break;
            }
            if (entries[i].column < current.column) {
                targetEntry = i;
                break;
            }
        }
    } else if (key == ImGuiKey_RightArrow) {
        for (int i = currentEntry + 1; i < static_cast<int>(entries.size()); ++i) {
            if (entries[i].row != current.row) {
                break;
            }
            if (entries[i].column > current.column) {
                targetEntry = i;
                break;
            }
        }
        if (targetEntry < 0 && currentEntry + 1 < static_cast<int>(entries.size())) {
            targetEntry = currentEntry + 1;
        }
    } else if (key == ImGuiKey_UpArrow) {
        targetEntry = chooseInRow(current.row - 1, current.column);
    } else if (key == ImGuiKey_DownArrow) {
        targetEntry = chooseInRow(current.row + 1, current.column);
    }

    if (targetEntry < 0 || targetEntry >= static_cast<int>(entries.size()) || targetEntry == currentEntry) {
        return false;
    }
    selectSingle(context, (*items)[entries[targetEntry].itemIndex]);
    return true;
}

void enterCurrentItemListFromCategory(AppContext& context)
{
    if (!context.runtime().selectedItemId.empty()) {
        return;
    }
    std::vector<LaunchItem>* items = currentItems(context);
    if (items && !items->empty()) {
        selectSingle(context, items->front());
    }
}

ItemEditorApi itemEditorApi()
{
    return ItemEditorApi{&editingItems, &selectSingle, &nowUnix};
}

SearchUiState searchUiState()
{
    return SearchUiState{&gSession.focusSearch,       &gSession.searchOpen,     &gSession.searchSubmit,
                         &gSession.searchSelected,    &gSession.searchMove,     &gSession.searchPageMove,
                         &gSession.searchQueryText,   &gSession.searchEditedAt, &gSession.searchCursorEndRequested,
                         &gSession.searchResultsCache};
}

void openNoteById(AppContext& context, const std::string& id)
{
    context.runtime().selectedNoteId = id;
    context.runtime().showNoteQuick = true;
    context.runtime().editSelectedNote = false;
}

void openNoteEditorById(AppContext& context, const std::string& id)
{
    context.runtime().selectedNoteId = id;
    context.runtime().showNotes = true;
    context.runtime().showNoteQuick = false;
    context.runtime().editSelectedNote = true;
}

SearchUiApi searchUiApi()
{
    return SearchUiApi{&findItemById,
                       [](AppContext& context, LaunchItem& item, int showCommand) {
                           runItem(context, item, showCommand);
                       },
                       &drawLaunchIcon,
                       &drawCachedLaunchIcon,
                       &requestLaunchIcon,
                       &openItemEditorWithDraft,
                       &openNoteById,
                       &copyItemToClipboard,
                       &pasteClipboardItem,
                       &clipboardAvailable,
                       &copyTextToClipboard,
                       &itemPropertiesText,
                       &openWithDialog,
                       &openContainingFolder,
                       &showFileProperties};
}

ContentMenuApi contentMenuApi()
{
    return ContentMenuApi{&drawViewMenu,
                          &drawSortMenu,
                          &currentItems,
                          &pasteClipboardItem,
                          &openItemEditor,
                          &appendPlaceholders,
                          &appendItem,
                          &appendNoteItem,
                          &appendBuiltInItem,
                          &runItemsInList,
                          &requestHideMainWindow,
                          []() {
                              clearIconTextureCache();
                          },
                          &requestDeleteIds};
}

ContentMenuState contentMenuState()
{
    return ContentMenuState{clipboardAvailable()};
}

TooltipApi tooltipApi()
{
    return TooltipApi{&timeText};
}

void addScheduledTaskForItem(AppContext& context, const LaunchItem& item)
{
    ScheduledTask task;
    task.id = "task-" + std::to_string(nowUnix()) + "-" + item.id;
    task.name = item.name;
    task.itemId = item.id;
    task.action = item.type == LaunchItemType::VirtualFolder ? ScheduledActionKind::LaunchVirtualFolder : ScheduledActionKind::LaunchItem;
    task.trigger = ScheduledTriggerKind::Daily;
    task.hour = 9;
    task.minute = 0;
    task.onceAt = nowUnix() + 10 * 60;
    context.persisted().scheduledTasks.push_back(std::move(task));
    context.save();
    gSession.showTaskPlanner = true;
}

ItemMenuApi itemMenuApi()
{
    return ItemMenuApi{&isItemSelected,
                       &selectSingle,
                       [](AppContext& context, int itemIndex) {
                           openItemEditor(context, itemIndex);
                       },
                       &requestDeleteSelection,
                       &enterVirtualFolder,
                       &copyItemToClipboard,
                       &copyTextToClipboard,
                       &itemPropertiesText,
                       [](AppContext& context, LaunchItem& item, int showCommand) {
                           runItem(context, item, showCommand);
                       },
                       [](AppContext& context, const LaunchItem& item) {
                           openNoteEditorById(context, item.target.string());
                       },
                       &openWithDialog,
                       &openContainingFolder,
                       &showFileProperties,
                       &rebuildIconCacheForSelection,
                       &addScheduledTaskForItem};
}

ItemViewApi itemViewApi()
{
    return ItemViewApi{&isItemSelected,
                       &handleItemSelectionClick,
                       &selectSingle,
                       &enterVirtualFolder,
                       [](AppContext& context, LaunchItem& item, int showCommand) {
                           runItem(context, item, showCommand);
                       },
                       [](const UiPalette& theme, AppContext& context, std::vector<LaunchItem>& items, int itemIndex) {
                           drawItemMenu(theme, context, items, itemIndex, itemMenuApi());
                       },
                       [](const UiPalette& theme, const AppSettings& settings, const LaunchItem& item, const ImRect& rect) {
                           drawItemTooltip(theme, settings, item, rect, tooltipApi());
                       },
                       &drawLaunchIcon,
                       &drawLaunchIconOnList,
                       &dragPayloadId,
                       &captureDragItemIds,
                       [](const std::string& key, const std::function<void()>& callback) {
                           triggerAfterDragHover(key, callback);
                       },
                       &dragHoverPending,
                       &showFileProperties};
}

CategoryRailApi categoryRailApi()
{
    return CategoryRailApi{&clearSelection,
                           [](AppContext& context, int itemIndex) {
                               openItemEditor(context, itemIndex);
                           },
                           &requestDeleteCategory,
                           &reorderCategory,
                           [](AppContext& context, const std::string& id, std::vector<LaunchItem>& destination) {
                               return moveItemByIdToList(context, id, destination);
                           },
                           [](AppContext& context, const std::vector<std::string>& ids, std::vector<LaunchItem>& destination) {
                               return moveItemIdsToList(context, ids, destination, -1);
                           },
                           [](const std::string& key, const std::function<void()>& callback) {
                               triggerAfterDragHover(key, callback);
                           },
                           &dragHoverPending,
                           &dragPayloadId,
                           &dragItemIds,
                           &runItemsInList,
                           &requestHideMainWindow};
}

CategoryRailState categoryRailState(AppContext& context)
{
    return CategoryRailState{&context.runtime().currentFolderStack, &gSession.openCategoryEditorPopup, &gSession.editingCategoryIndex,
                             &gSession.editingCategoryName,         &gSession.editingCategoryIconName, &gSession.editingCategoryIconColor,
                             &gSession.categoryIconFilter};
}

MainDockGridState mainDockGridState(AppContext& context)
{
    return MainDockGridState{&context.runtime().currentFolderStack, &gSession.resetMainDockScroll};
}

MainDockGridApi mainDockGridApi()
{
    return MainDockGridApi{[](AppContext& context) {
                               return currentItems(context);
                           },
                           &itemsForFolderStack,
                           &clearSelection,
                           &requestHideMainWindow,
                           &itemIndexById,
                           &moveItemByIdToList,
                           &moveItemIdsToList,
                           [](AppContext& context, const std::string& id) {
                               return findItemById(context, id);
                           },
                           &dragPayloadId,
                           &dragItemIds,
                           [](const std::string& key, const std::function<void()>& callback) {
                               triggerAfterDragHover(key, callback);
                           },
                           &dragHoverPending,
                           [](const UiPalette& theme, AppContext& context, const ContentMenuState& state, const ContentMenuApi& api) {
                               drawContentMenu(theme, context, state, api);
                           },
                           contentMenuApi(),
                           &contentMenuState,
                           itemViewApi()};
}

MainDockStateApi mainDockStateApi()
{
    return MainDockStateApi{[](AppContext& context, const std::string& id) {
                                return findItemById(context, id);
                            },
                            [](const AppContext& context, const std::string& id) {
                                return findItemById(context, id);
                            },
                            [](AppContext& context) {
                                return currentItems(context);
                            },
                            &removeItemById,
                            &clearIconCacheForItem,
                            &nowUnix};
}

void handleMainShortcuts(AppContext& context)
{
    ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        if (!selectedItemIds(context).empty()) {
            requestDeleteSelection(context);
            return;
        }
        if (!gSession.searchOpen && context.runtime().selectedCategory >= 0 &&
            context.runtime().selectedCategory < static_cast<int>(context.persisted().categories.size()) &&
            context.persisted().categories.size() > 1) {
            requestDeleteCategory(context, context.runtime().selectedCategory);
            return;
        }
    }
    if (io.WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        return;
    }
    if (gSession.searchOpen) {
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            resetSearchState(context);
        } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
            gSession.searchPageMove = -1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
            gSession.searchPageMove = 1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
            gSession.searchMove = 1;
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
            gSession.searchMove = -1;
        }
        return;
    }

    if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
        switchSelectedCategory(context, -1);
    } else if (io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
        switchSelectedCategory(context, 1);
    } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
        if (context.runtime().selectedItemId.empty()) {
            switchSelectedCategory(context, 1);
        } else {
            moveCurrentItemSelection(context, ImGuiKey_DownArrow);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
        if (context.runtime().selectedItemId.empty()) {
            switchSelectedCategory(context, -1);
        } else {
            moveCurrentItemSelection(context, ImGuiKey_UpArrow);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
        if (context.runtime().selectedItemId.empty() || !moveCurrentItemSelection(context, ImGuiKey_LeftArrow)) {
            clearSelection(context);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
        if (context.runtime().selectedItemId.empty()) {
            enterCurrentItemListFromCategory(context);
        } else {
            moveCurrentItemSelection(context, ImGuiKey_RightArrow);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
        return;
    } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false)) {
        if (LaunchItem* item = findItemById(context, context.runtime().selectedItemId)) {
            runItem(context, *item);
        }
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false)) {
        selectAllCurrentCategory(context);
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
        if (const LaunchItem* item = findItemById(context, context.runtime().selectedItemId)) {
            copyItemToClipboard(*item, false);
        }
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
        if (const LaunchItem* item = findItemById(context, context.runtime().selectedItemId)) {
            copyItemToClipboard(*item, true);
        }
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
        pasteClipboardItem(context);
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        openItemEditor(context, -1, LaunchItemType::App);
    } else if (ImGui::IsKeyPressed(ImGuiKey_F2, false)) {
        // F2 always opens the item properties editor.
        if (const LaunchItem* item = findItemById(context, context.runtime().selectedItemId)) {
            if (std::vector<LaunchItem>* items = currentItems(context)) {
                for (int i = 0; i < static_cast<int>(items->size()); ++i) {
                    if ((*items)[i].id == item->id) {
                        openItemEditor(context, i);
                        break;
                    }
                }
            }
        }
    } else if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
        // Ctrl+E opens note content editor for note items.
        if (const LaunchItem* item = findItemById(context, context.runtime().selectedItemId)) {
            if (item->type == LaunchItemType::Note) {
                openNoteEditorById(context, item->target.string());
            }
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        clearSelection(context);
    }
}

void startMainWindowDrag(AppContext& context)
{
    const bool lockPosition = context.persisted().settings.lockWindowPosition;
    ImGui::CloseCurrentPopup();
    gSession.draggingMainWindow = false;
    if (lockPosition || !mouseOverMainWindow()) {
        return;
    }
    if (HWND hwnd = mainWindowHandle()) {
        RECT rect{};
        if (GetWindowRect(hwnd, &rect)) {
            gSession.draggingMainWindow = true;
            gSession.mainDragStartMouse = ImGui::GetIO().MousePos;
            gSession.mainDragStartRect = rect;
        }
    }
}

void updateMainWindowDrag(AppContext& context)
{
    const bool lockPosition = context.persisted().settings.lockWindowPosition;
    if (!gSession.draggingMainWindow) {
        return;
    }
    if (lockPosition || !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        gSession.draggingMainWindow = false;
        return;
    }

    HWND hwnd = mainWindowHandle();
    if (!hwnd) {
        gSession.draggingMainWindow = false;
        return;
    }

    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const int x = gSession.mainDragStartRect.left + static_cast<int>(std::lround(mouse.x - gSession.mainDragStartMouse.x));
    const int y = gSession.mainDragStartRect.top + static_cast<int>(std::lround(mouse.y - gSession.mainDragStartMouse.y));
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void updateMainWindowTitleDrag(AppContext& context)
{
    if (ImGui::IsItemActivated()) {
        startMainWindowDrag(context);
    }
}

void handleBlankAreaWindowDrag(AppContext& context, const ImVec2& origin, const ImVec2& size)
{
    const AppSettings& settings = context.persisted().settings;
    if (!settings.dragBlankAreaMoveWindow || settings.lockWindowPosition) {
        return;
    }
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || ImGui::IsAnyItemActive() || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
        return;
    }
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsAnyItemHovered() || !mouseOverMainWindow() ||
        !ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        return;
    }
    const ImVec2 mouse = io.MousePos;
    const ImRect body(ImVec2(origin.x, origin.y + kTitleHeight), ImVec2(origin.x + size.x, origin.y + size.y));
    if (!body.Contains(mouse)) {
        return;
    }
    startMainWindowDrag(context);
}
void drawTitleBar(AppContext& context, const ImVec2& origin, const ImVec2& size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const bool hasBackgroundImage = hasThemeBackground(context.themes.active());
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + kTitleHeight),
                      hasBackgroundImage ? ImGui::ColorConvertFloat4ToU32(withAlpha(gSession.theme.titleBar, 0.82f))
                                         : gSession.theme.titleBar,
                      gSession.theme.windowRounding, ImDrawFlags_RoundCornersTop);
    dl->AddText(ImVec2(origin.x + 16.0f, origin.y + 13.0f), gSession.theme.text, "Launcher");
    float buttonX = origin.x + size.x;
    const auto nextButtonPos = [&]() {
        buttonX -= 58.0f;
        return ImVec2(buttonX, origin.y);
    };
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y));
    const float visibleButtonWidth = (context.persisted().settings.showCloseButton ? 58.0f : 0.0f) +
                                     (context.persisted().settings.showMenuButton ? 58.0f : 0.0f) +
                                     (context.persisted().settings.showSearchButton ? 58.0f : 0.0f);
    ImGui::InvisibleButton("title-drag-close-popups", ImVec2(std::max(0.0f, size.x - visibleButtonWidth), kTitleHeight));
    updateMainWindowTitleDrag(context);

    if (context.persisted().settings.showCloseButton &&
        drawTitleButton(gSession.theme, kTitleHeight, "close-button", nextButtonPos(), Icons::Close)) {
        if (auto* hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw)) {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
    }
    if (context.persisted().settings.showMenuButton &&
        drawTitleButton(gSession.theme, kTitleHeight, "menu-button", nextButtonPos(), Icons::Menu)) {
        ImGui::OpenPopup("top-menu");
    }
    if (context.persisted().settings.showSearchButton &&
        drawTitleButton(gSession.theme, kTitleHeight, "search-button", nextButtonPos(), Icons::Search, gSession.searchOpen)) {
        gSession.searchOpen = !gSession.searchOpen;
        if (gSession.searchOpen) {
            gSession.focusSearch = true;
        } else {
            resetSearchState(context);
        }
    }

    LightPopupStyle popupStyle(gSession.theme);
    const bool animatedPopup = ui_anim::pushPopupAppear("top-menu");
    if (ImGui::BeginPopup("top-menu")) {
        suppressCurrentViewportNativeBorder();
        if (beginIconMenu(gSession.theme, Icons::Tools, tr("Tools"))) {
            if (beginIconMenu(gSession.theme, Icons::DataManagement, tr("Data Management"))) {
                if (menuItem(gSession.theme, "", tr("Import Config"))) importConfig(context);
                if (menuItem(gSession.theme, "", tr("Export Config"))) exportConfig(context);
                endIconMenu();
            }
            if (menuItem(gSession.theme, Icons::Folder, tr("Open Location"))) openAppFolder();
            if (menuItem(gSession.theme, Icons::Theme, tr("Theme Editor"))) context.runtime().showThemeEditor = true;
            if (menuItem(gSession.theme, Icons::Task, tr("Task Manager"))) openSystemTool(L"taskmgr.exe");
            endIconMenu();
        }
        if (beginIconMenu(gSession.theme, Icons::QuickSettings, tr("Quick Settings"))) {
            if (menuToggleItem(gSession.theme, Icons::DoNotDisturb, tr("Do Not Disturb"),
                               context.persisted().settings.fullscreenDoNotDisturb)) {
                context.persisted().settings.fullscreenDoNotDisturb = !context.persisted().settings.fullscreenDoNotDisturb;
                context.save();
            }
            if (menuToggleItem(gSession.theme, Icons::Pin, tr("Keep Shown"), context.persisted().settings.alwaysOnTop)) {
                context.persisted().settings.alwaysOnTop = !context.persisted().settings.alwaysOnTop;
                context.save();
            }
            ImGui::Separator();
            if (menuToggleItem(gSession.theme, Icons::TopMost, tr("Always on Top"), context.persisted().settings.alwaysOnTop)) {
                context.persisted().settings.alwaysOnTop = !context.persisted().settings.alwaysOnTop;
                context.save();
            }
            if (menuItem(gSession.theme, Icons::CenterWindow, tr("Reset Position"))) centerMainWindow();
            if (menuToggleItem(gSession.theme, Icons::Pin, tr("Lock Position"), context.persisted().settings.lockWindowPosition)) {
                context.persisted().settings.lockWindowPosition = !context.persisted().settings.lockWindowPosition;
                context.save();
            }
            if (menuToggleItem(gSession.theme, Icons::Pin, tr("Lock Size"), context.persisted().settings.lockWindowSize)) {
                context.persisted().settings.lockWindowSize = !context.persisted().settings.lockWindowSize;
                context.save();
            }
            if (menuToggleItem(gSession.theme, Icons::Layout, tr("Lock Layout"), context.persisted().settings.lockItemLayout)) {
                context.persisted().settings.lockItemLayout = !context.persisted().settings.lockItemLayout;
                context.save();
            }
            endIconMenu();
        }
        if (menuItem(gSession.theme, Icons::Task, tr("Task Planner"))) gSession.showTaskPlanner = true;
        if (menuItem(gSession.theme, Icons::Note, tr("Notes"))) {
            context.runtime().showNotes = true;
            context.runtime().showNoteQuick = false;
            context.runtime().editSelectedNote = false;
        }
        if (menuItem(gSession.theme, Icons::Plugin, tr("Plugins"))) requestSettingsTab(context, 4);
        ImGui::Separator();
        if (menuItem(gSession.theme, Icons::Refresh, tr("Check Updates"))) {
            gSession.showUpdateDialog = true;
            context.updates.checkForUpdates();
        }
        if (menuItem(gSession.theme, Icons::Help, tr("Help"))) context.runtime().showUserGuide = true;
        if (menuItem(gSession.theme, Icons::Settings, tr("Settings"))) {
            context.runtime().showSettings = false;
            gSession.openSettingsNextFrame = true;
        }
        if (menuItem(gSession.theme, Icons::Close, tr("Exit"))) {
            if (auto* hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw)) {
                PostMessageW(hwnd, WM_COMMAND, 3002, 0);
            }
        }
        ImGui::EndPopup();
    }
    if (animatedPopup) {
        ui_anim::popAppearAlpha();
    }
}

bool drawChoiceParamCombo(const UiPalette& theme, const char* id, const InteractiveParam& param, std::string& value)
{
    if (param.choices.empty()) {
        return ImGui::InputTextWithHint(id, tr("Enter value"), &value);
    }

    if (value.empty()) {
        value = param.choices.front();
    }
    bool changed = false;
    const bool comboOpen = beginStyledCombo(theme, id, value.c_str());
    if (comboOpen) {
        ui_anim::pushAppearAlpha(ImGui::GetID(id), 0.10f, 0.20f);
        for (const std::string& choice : param.choices) {
            if (styledComboItem(theme, choice.c_str(), choice == value)) {
                value = choice;
                changed = true;
            }
        }
        ui_anim::popAppearAlpha();
    }
    endStyledCombo(comboOpen);
    return changed;
}

bool drawInteractiveHistorySuggestions(AppContext& context, const UiPalette& theme, int paramIndex, std::string& value, bool inputActive)
{
    if (paramIndex < 0 || paramIndex >= static_cast<int>(gSession.interactiveRunItem.interactiveParams.size())) {
        return false;
    }

    const InteractiveParam& param = gSession.interactiveRunItem.interactiveParams[static_cast<size_t>(paramIndex)];
    const std::string key = interactiveParamKey(gSession.interactiveRunItemId, param, paramIndex);
    bool changed = false;
    const bool keyboardActive = inputActive || gSession.interactiveHistoryParamKey == key;
    if (keyboardActive) {
        if (gSession.interactiveHistoryParamKey != key) {
            gSession.interactiveHistoryParamKey = key;
            gSession.interactiveHistorySelected = -1;
        }
    } else if (gSession.interactiveHistoryParamKey != key) {
        return false;
    }

    std::vector<InteractiveHistoryCandidate> candidates = interactiveHistoryCandidates(param, value);
    if (candidates.empty()) {
        return false;
    }
    if (gSession.interactiveHistorySelected >= static_cast<int>(candidates.size())) {
        gSession.interactiveHistorySelected = static_cast<int>(candidates.size()) - 1;
    }
    if (gSession.interactiveHistorySelected < -1) {
        gSession.interactiveHistorySelected = -1;
    }

    if (inputActive) {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, false)) {
            gSession.interactiveHistorySelected = gSession.interactiveHistorySelected < 0
                                                      ? 0
                                                      : (gSession.interactiveHistorySelected + 1) % static_cast<int>(candidates.size());
        } else if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, false)) {
            gSession.interactiveHistorySelected =
                gSession.interactiveHistorySelected < 0
                    ? static_cast<int>(candidates.size()) - 1
                    : (gSession.interactiveHistorySelected + static_cast<int>(candidates.size()) - 1) % static_cast<int>(candidates.size());
        } else if (ImGui::IsKeyPressed(ImGuiKey_Tab, false)) {
            gSession.interactiveHistorySelected = gSession.interactiveHistorySelected < 0
                                                      ? 0
                                                      : (gSession.interactiveHistorySelected + 1) % static_cast<int>(candidates.size());
        } else if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
            gSession.interactiveHistorySelected = gSession.interactiveHistorySelected < 0 ? 0 : gSession.interactiveHistorySelected;
            value = candidates[static_cast<size_t>(gSession.interactiveHistorySelected)].value;
            gSession.interactiveHistoryParamKey.clear();
            gSession.interactiveHistorySelected = -1;
            ImGui::ClearActiveID();
            return true;
        }
    }

    const float rowHeight = ImGui::GetTextLineHeightWithSpacing() + 6.0f;
    const ImVec2 historyPadding(6.0f, 4.0f);
    const float height = std::min(4, static_cast<int>(candidates.size())) * rowHeight + historyPadding.y * 2.0f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, historyPadding);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, theme.popupBg);
    ImGui::BeginChild("history", ImVec2(-1.0f, height), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (int i = 0; i < static_cast<int>(candidates.size()); ++i) {
        const InteractiveHistoryCandidate& candidate = candidates[static_cast<size_t>(i)];
        ImGui::PushID(i);
        const float buttonWidth = 30.0f;
        const ImVec2 rowPos = ImGui::GetCursorScreenPos();
        const float rowWidth = ImGui::GetContentRegionAvail().x;
        const float pickWidth = std::max(40.0f, rowWidth - buttonWidth - 4.0f);
        const ImVec2 pickMax(rowPos.x + pickWidth, rowPos.y + rowHeight);
        const ImVec2 removePos(rowPos.x + rowWidth - buttonWidth, rowPos.y);
        const ImVec2 removeMax(removePos.x + buttonWidth, removePos.y + rowHeight);
        const bool selected = gSession.interactiveHistorySelected >= 0 && i == gSession.interactiveHistorySelected;
        const std::string label = candidate.value + "  (" + std::to_string(std::max(1, candidate.useCount)) + ")";
        if (selected) {
            dl->AddRectFilled(rowPos, pickMax, ImGui::GetColorU32(theme.headerActive), 4.0f);
        } else if (ImGui::IsMouseHoveringRect(rowPos, pickMax)) {
            dl->AddRectFilled(rowPos, pickMax, ImGui::GetColorU32(theme.headerHovered), 4.0f);
        }
        if (ImGui::IsMouseHoveringRect(removePos, removeMax)) {
            dl->AddRectFilled(removePos, removeMax, ImGui::GetColorU32(theme.headerHovered), 4.0f);
        }
        ImGui::InvisibleButton("pick", ImVec2(pickWidth, rowHeight));
        if (ImGui::IsItemClicked()) {
            value = candidate.value;
            gSession.interactiveHistorySelected = i;
            changed = true;
        }
        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
        dl->AddText(ImVec2(rowPos.x + 8.0f, rowPos.y + (rowHeight - textSize.y) * 0.5f), theme.text, label.c_str());
        ImGui::SetCursorScreenPos(removePos);
        ImGui::InvisibleButton("remove", ImVec2(buttonWidth, rowHeight));
        const ImVec2 removeMin = ImGui::GetItemRectMin();
        const ImVec2 closeSize = ImGui::CalcTextSize(Icons::Close);
        dl->AddText(ImVec2(removeMin.x + (buttonWidth - closeSize.x) * 0.5f, removeMin.y + (rowHeight - closeSize.y) * 0.5f),
                    theme.textMuted, Icons::Close);
        if (ImGui::IsItemClicked()) {
            const std::string removed = candidate.value;
            removeInteractiveHistoryValue(gSession.interactiveRunItem, paramIndex, removed);
            if (LaunchItem* liveItem = findItemById(context, gSession.interactiveRunItemId)) {
                removeInteractiveHistoryValue(*liveItem, paramIndex, removed);
                context.save();
            }
            const int remaining = static_cast<int>(candidates.size()) - 1;
            if (remaining <= 0) {
                gSession.interactiveHistorySelected = -1;
            } else if (gSession.interactiveHistorySelected >= remaining) {
                gSession.interactiveHistorySelected = remaining - 1;
            }
            changed = true;
        }
        ImGui::SetCursorScreenPos(ImVec2(rowPos.x, rowPos.y + rowHeight));
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    return changed;
}

void drawInteractiveRunDialog(AppContext& context, const UiPalette& theme)
{
    if (!gSession.showInteractiveRun) {
        return;
    }

    setupManagedWindow("LauncherInteractiveRun");
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440.0f, 420.0f), ImGuiCond_Appearing);
    if (gSession.openInteractiveRunPopup) {
        ImGui::SetNextWindowFocus();
        gSession.openInteractiveRunPopup = false;
    }

    ManagedWindowStyle windowStyle(theme);
    bool open = true;
    if (ImGui::Begin(tr("Run###interactive-run"), &open,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoSavedSettings)) {
        applyManagedViewportChrome(ImGui::GetWindowViewport()->PlatformHandleRaw, context.themes.active(), theme);
        drawManagedTitleBar(theme, tr("Run"), open);
        if (!open) {
            gSession.showInteractiveRun = false;
            gSession.interactiveHistoryParamKey.clear();
            gSession.interactiveHistorySelected = -1;
            ImGui::End();
            return;
        }

        ImGui::SetCursorPos(ImVec2(14.0f, kUiTitleHeight + 12.0f));
        ImGui::BeginChild("interactive-run-content", ImVec2(-14.0f, -58.0f), ImGuiChildFlags_None);
        ImGui::TextUnformatted(gSession.interactiveRunItem.name.c_str());
        if (!gSession.interactiveRunItem.remark.empty()) {
            ImGui::TextDisabled("%s", gSession.interactiveRunItem.remark.c_str());
        }
        ImGui::Dummy(ImVec2(1.0f, 8.0f));

        const std::vector<InteractiveParam>& params = gSession.interactiveRunItem.interactiveParams;
        while (gSession.interactiveRunValues.size() < params.size()) {
            gSession.interactiveRunValues.push_back(defaultParamValue(params[gSession.interactiveRunValues.size()]));
        }

        for (int i = 0; i < static_cast<int>(params.size()); ++i) {
            const InteractiveParam& param = params[static_cast<size_t>(i)];
            std::string& value = gSession.interactiveRunValues[static_cast<size_t>(i)];
            const std::string label = param.label.empty() ? effectiveParamId(param, i) : param.label;
            ImGui::TextUnformatted(label.c_str());
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::PushID(i);
            if (param.kind == InteractiveParamKind::Number) {
                double number = value.empty() ? param.minValue : std::strtod(value.c_str(), nullptr);
                if (param.maxValue >= param.minValue) {
                    number = std::clamp(number, param.minValue, param.maxValue);
                }
                const double step = param.step <= 0.0 ? 1.0 : param.step;
                if (ImGui::InputDouble("##value", &number, step, step * 10.0, "%.6g")) {
                    if (param.maxValue >= param.minValue) {
                        number = std::clamp(number, param.minValue, param.maxValue);
                    }
                    char buffer[64]{};
                    std::snprintf(buffer, sizeof(buffer), "%.6g", number);
                    value = buffer;
                }
                const bool inputActive = ImGui::IsItemActive() || ImGui::IsItemFocused();
                drawInteractiveHistorySuggestions(context, theme, i, value, inputActive);
            } else if (param.kind == InteractiveParamKind::Choice) {
                drawChoiceParamCombo(theme, "##value", param, value);
            } else {
                ImGui::InputTextWithHint("##value", tr("Enter value"), &value);
                const bool inputActive = ImGui::IsItemActive() || ImGui::IsItemFocused();
                drawInteractiveHistorySuggestions(context, theme, i, value, inputActive);
            }
            ImGui::PopID();
            ImGui::Dummy(ImVec2(1.0f, 6.0f));
        }
        ImGui::EndChild();

        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 194.0f);
        if (ImGui::Button(tr("Run"), ImVec2(86.0f, 32.0f))) {
            LaunchItem launchItem = withInteractiveValues(gSession.interactiveRunItem, gSession.interactiveRunValues);
            if (!gSession.interactiveRunSearchText.empty()) {
                launchItem = withSearchVariables(launchItem, gSession.interactiveRunSearchText);
            }
            bool launched = false;
            if (LaunchItem* liveItem = findItemById(context, gSession.interactiveRunItemId)) {
                launched = launchResolvedItem(context, *liveItem, std::move(launchItem), gSession.interactiveRunShowCommand);
                if (launched) {
                    recordInteractiveHistory(*liveItem, gSession.interactiveRunValues);
                    context.save();
                }
            } else {
                LaunchItem fallback = gSession.interactiveRunItem;
                launched = launchResolvedItem(context, fallback, std::move(launchItem), gSession.interactiveRunShowCommand);
            }
            if (launched) {
                gSession.showInteractiveRun = false;
                gSession.interactiveHistoryParamKey.clear();
                gSession.interactiveHistorySelected = -1;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button(tr("Cancel"), ImVec2(86.0f, 32.0f))) {
            gSession.showInteractiveRun = false;
            gSession.interactiveHistoryParamKey.clear();
            gSession.interactiveHistorySelected = -1;
        }
    }
    ImGui::End();
}

void enterVirtualFolder(AppContext& context, const LaunchItem& item)
{
    if (item.type != LaunchItemType::VirtualFolder) {
        return;
    }
    context.runtime().currentFolderStack.push_back(item.id);
    clearSelection(context);
}

} // namespace

void closeMainDockSearch(AppContext& context)
{
    resetSearchState(context);
}

void openMainDockSearch(AppContext& context)
{
    gSession.searchOpen = true;
    gSession.focusSearch = true;
    gSession.searchSubmit = false;
    gSession.searchSelected = 0;
    gSession.searchMove = 0;
    gSession.searchPageMove = 0;
    gSession.searchEditedAt = ImGui::GetTime();
    gSession.searchQueryText = context.runtime().searchText;
}

void openMainDockSearchWithText(AppContext& context, std::string text)
{
    context.runtime().searchText = std::move(text);
    openMainDockSearch(context);
    gSession.searchSubmit = false;
    gSession.searchQueryText = context.runtime().searchText;
    gSession.searchEditedAt = ImGui::GetTime();
    gSession.searchCursorEndRequested = true;
}

void setMainDockDevice(ID3D11Device* device)
{
    if (gSession.d3dDevice != device) {
        clearIconTextureCache();
        resetBackgroundTextureCache();
        resetIconLoadScheduling();
    }
    gSession.d3dDevice = device;
}

void releaseMainDockCaches()
{
    clearIconTextureCache();
    resetIconLoadScheduling();
    resetBackgroundTextureCache();
}

void releaseMainDockBackgroundCache()
{
    resetBackgroundTextureCache();
}

void closeMainDockWindows(AppContext& context)
{
    context.runtime().showSettings = false;
    context.runtime().showThemeEditor = false;
    context.runtime().showNotes = false;
    context.runtime().showNoteQuick = false;
    context.runtime().editSelectedNote = false;
    if (context.persisted().settings.hideSearchAfterMainClose) {
        resetSearchState(context);
    }
    gSession.showTaskPlanner = false;
    gSession.openSettingsNextFrame = false;
    gSession.showItemEditor = false;
    gSession.openItemEditorPopup = false;
    gSession.editingCategory = -1;
    gSession.editingItem = -1;
    gSession.editingFolderId.clear();
    gSession.editingDraft = {};
    gSession.editingTarget.clear();
    gSession.editingStartDir.clear();
    gSession.editingRemark.clear();
    gSession.editingIcon.clear();
    gSession.showInteractiveRun = false;
    gSession.openInteractiveRunPopup = false;
    gSession.interactiveRunItem = {};
    gSession.interactiveRunItemId.clear();
    gSession.interactiveRunSearchText.clear();
    gSession.interactiveRunValues.clear();
    gSession.interactiveHistoryParamKey.clear();
    gSession.interactiveHistorySelected = -1;
    gSession.showBuildInfo = false;
    gSession.openCategoryEditorPopup = false;
    gSession.editingCategoryIndex = -1;
    gSession.editingCategoryName.clear();
    gSession.editingCategoryIconName.clear();
    gSession.editingCategoryIconColor.clear();
    gSession.categoryIconFilter.clear();
    clearDeleteState();
    clearClipboardState();
    clearDragHoverState();
    clearDragItemIdsSnapshot();
    clearMainDockDragVisualState();
    if (context.persisted().settings.clearSelectionAfterMainClose) {
        clearSelection(context);
    }
    if (ImGui::GetCurrentContext() != nullptr) {
        ImGuiIO& io = ImGui::GetIO();
        io.ClearInputKeys();
        io.ClearInputMouse();
        ImGui::ClearActiveID();
        ImGui::ClearDragDrop();
        ImGui::ClosePopupsExceptModals();
    }
}

void resetMainDockScrollOnNextFrame()
{
    gSession.resetMainDockScroll = true;
}

void drawUpdateDialog(AppContext& context)
{
    if (!gSession.showUpdateDialog) {
        return;
    }

    setupManagedWindow("LauncherManagedUpdates");
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520.0f, 390.0f), ImGuiCond_Appearing);
    ManagedWindowStyle windowStyle(gSession.theme);

    bool open = true;
    if (!ImGui::Begin("Check Updates###update-dialog", &open,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::End();
        if (!open) {
            gSession.showUpdateDialog = false;
        }
        return;
    }
    applyManagedViewportChrome(ImGui::GetWindowViewport()->PlatformHandleRaw, context.themes.active(), gSession.theme);
    drawManagedTitleBar(gSession.theme, tr("Check Updates"), open);
    ImGui::SetCursorPos(ImVec2(18.0f, kUiTitleHeight + 16.0f));
    ImGui::BeginChild("update-content", ImVec2(-18.0f, -18.0f), false);

    const UpdateSnapshot update = context.updates.snapshot();
    ImGui::Text("%s: %s", tr("Current version"), update.currentVersion.c_str());
    if (!update.latestVersion.empty()) {
        ImGui::SameLine();
        ImGui::Text("%s: %s", tr("Latest version"), update.latestVersion.c_str());
    }
    ImGui::Separator();

    switch (update.state) {
    case UpdateState::Idle:
        ImGui::TextUnformatted(tr("Ready to check for updates."));
        if (ImGui::Button(tr("Check Now"))) {
            context.updates.checkForUpdates();
        }
        break;
    case UpdateState::Checking: ImGui::TextUnformatted(tr("Checking for updates...")); break;
    case UpdateState::UpToDate:
        ImGui::TextUnformatted(tr("You are using the latest version."));
        if (ImGui::Button(tr("Check Again"))) {
            context.updates.checkForUpdates();
        }
        break;
    case UpdateState::Available:
        ImGui::TextUnformatted(tr("A new verified update is available."));
        if (ImGui::Button(tr("Download Update"))) {
            context.updates.downloadUpdate();
        }
        break;
    case UpdateState::Downloading:
        ImGui::TextUnformatted(tr("Downloading and verifying update..."));
        ImGui::ProgressBar(static_cast<float>(update.downloadPercent) / 100.0f, ImVec2(-1.0f, 0.0f),
                           (std::to_string(update.downloadPercent) + "%").c_str());
        break;
    case UpdateState::ReadyToInstall:
        ImGui::TextUnformatted(tr("Update verified and ready to install."));
        if (ImGui::Button(tr("Restart and Install"))) {
            if (context.updates.installDownloadedUpdate()) {
                if (auto* hwnd = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw)) {
                    PostMessageW(hwnd, WM_COMMAND, 3002, 0);
                }
            }
        }
        break;
    case UpdateState::Installing: ImGui::TextUnformatted(tr("Restarting to install update...")); break;
    case UpdateState::Failed:
        ImGui::TextUnformatted(tr("Update failed."));
        ImGui::TextWrapped("%s", update.message.c_str());
        if (ImGui::Button(tr("Try Again"))) {
            context.updates.checkForUpdates();
        }
        break;
    }

    if (!update.releaseNotes.empty()) {
        ImGui::Separator();
        ImGui::TextUnformatted(tr("Release notes"));
        ImGui::BeginChild("update-release-notes", ImVec2(-1.0f, 130.0f), true);
        ImGui::TextWrapped("%s", update.releaseNotes.c_str());
        ImGui::EndChild();
    }
    ImGui::Separator();
    if (ImGui::Button(tr("Close"))) {
        open = false;
    }
    ImGui::EndChild();
    ImGui::End();
    if (!open) {
        gSession.showUpdateDialog = false;
    }
}

void drawMainDock(AppContext& context)
{
    configureMainDockState(mainDockStateApi());
    resolvePendingItemSelectionClick(context);
    setLocale(context.persisted().settings.language);
    applyUiStyle(context.themes.active());
    gSession.theme = uiPalette(context.themes.active());
    const UpdateSnapshot updateSnapshot = context.updates.snapshot();
    if (updateSnapshot.state == UpdateState::Checking && updateSnapshot.automaticCheck) {
        gSession.automaticUpdatePromptVersion.clear();
    } else if (updateSnapshot.state == UpdateState::Available && updateSnapshot.automaticCheck && !updateSnapshot.latestVersion.empty() &&
               gSession.automaticUpdatePromptVersion != updateSnapshot.latestVersion) {
        gSession.automaticUpdatePromptVersion = updateSnapshot.latestVersion;
        gSession.showUpdateDialog = true;
    }
    if (gSession.useDefaultIcons != context.persisted().settings.useDefaultIcons) {
        gSession.useDefaultIcons = context.persisted().settings.useDefaultIcons;
        clearIconTextureCache();
        resetIconLoadScheduling();
        trimCurrentProcessWorkingSet();
    }
    if (gSession.openSettingsNextFrame) {
        context.runtime().showSettings = true;
        gSession.openSettingsNextFrame = false;
    }
    if (context.runtime().showThemeEditor && !gSession.themeEditorWasOpen) {
        clearIconTextureCache();
        resetIconLoadScheduling();
    }
    gSession.themeEditorWasOpen = context.runtime().showThemeEditor;

    ImGuiIO& io = ImGui::GetIO();
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImVec2 origin = viewport->Pos;
    const ImVec2 size(io.DisplaySize.x, io.DisplaySize.y);
    ImGui::SetNextWindowPos(origin);
    ImGui::SetNextWindowSize(size);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::SetNextWindowScroll(ImVec2(0.0f, 0.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::Begin("LauncherRoot", nullptr, flags);
    const bool hasBackgroundImage = hasThemeBackground(context.themes.active());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), gSession.theme.contentBg, gSession.theme.windowRounding);
    if (hasBackgroundImage) {
        drawConfiguredBackground(context, context.themes.active(), origin, size);
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        handleMainShortcuts(context);
    }
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && !ImGui::IsAnyItemActive() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
        gSession.searchOpen = true;
        gSession.focusSearch = true;
    }
    drawTitleBar(context, origin, size);
    drawBuildInfoPopup(context.themes.active(), gSession.theme, gSession.showBuildInfo, &openAppFolder);
    drawDeleteConfirmPopup(gSession.theme, context, takeDeleteConfirmState(), &deletePendingItems);

    const ImVec2 bodyOrigin(origin.x, origin.y + kTitleHeight);
    const float bodyHeight = std::max(0.0f, size.y - kTitleHeight);
    if (!gSession.searchOpen) {
        drawCategoryRail(gSession.theme, context, categoryRailState(context), categoryRailApi(), bodyOrigin, bodyHeight, kRailWidth);

        const ImVec2 contentOrigin(origin.x + kRailWidth, bodyOrigin.y);
        const ImVec2 contentSize(std::max(0.0f, size.x - kRailWidth), bodyHeight);
        beginIconLoadFrame(context);
        drawItemGrid(gSession.theme, context, mainDockGridState(context), mainDockGridApi(), contentOrigin, contentSize);
    } else {
        drawSearchBar(gSession.theme, context, searchUiState(), bodyOrigin, size.x, kSearchHeight);
        beginIconLoadFrame(context);
        drawSearchResults(gSession.theme, context, searchUiState(), searchUiApi(), ImVec2(bodyOrigin.x, bodyOrigin.y + kSearchHeight),
                          ImVec2(size.x, std::max(0.0f, bodyHeight - kSearchHeight)));
    }
    processPendingIconRequests();
    handleBlankAreaWindowDrag(context, origin, size);
    updateMainWindowDrag(context);
    if (!context.persisted().settings.lockWindowSize) {
        drawResizeHandles(origin, size, kMinWindowWidth, kMinWindowHeight);
    }
    if (gSession.theme.windowOutlineSize > 0.0f) {
        const float inset = gSession.theme.windowOutlineSize * 0.5f;
        dl->AddRect(ImVec2(origin.x + inset, origin.y + inset), ImVec2(origin.x + size.x - inset, origin.y + size.y - inset),
                    ImGui::ColorConvertFloat4ToU32(gSession.theme.windowOutline), gSession.theme.windowRounding, 0,
                    gSession.theme.windowOutlineSize);
    }
    ImGui::End();
    ImGui::PopStyleColor();

    drawItemEditor(gSession.theme, context,
                   {&gSession.showItemEditor, &gSession.openItemEditorPopup, &gSession.editingCategory, &gSession.editingFolderId,
                    &gSession.editingItem, &gSession.editingDraft, &gSession.editingTarget, &gSession.editingStartDir,
                    &gSession.editingRemark, &gSession.editingIcon},
                   itemEditorApi());
    drawSettingsPanel(context);
    drawThemeEditor(context);
    drawNotesPanel(context, gSession.theme);
    drawTaskPlannerWindow(context, context.themes.active(), gSession.theme, gSession.showTaskPlanner);
    drawUserGuideWindow(context, gSession.theme);
    drawInteractiveRunDialog(context, gSession.theme);
    drawUpdateDialog(context);
    snapMainWindowIfNeeded(context.persisted().settings);
}

} // namespace launcher
