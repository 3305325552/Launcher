#include "ui/rendering/MainDockResources.hpp"

#include "app/AppContext.hpp"
#include "core/AnimatedBackground.hpp"
#include "ui/platform/UiPlatform.hpp"
#include "ui/common/UiTheme.hpp"

#include <windows.h>
#include <commctrl.h>
#include <commoncontrols.h>
#include <imgui.h>
#include <shellapi.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace launcher {
namespace {

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

struct Texture {
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    int width = 0;
    int height = 0;
};

struct CachedTexture {
    std::unique_ptr<Texture> texture;
    size_t byteSize = 0;
    int lastUsedFrame = 0;
};

struct IconCache {
    std::unordered_map<std::string, CachedTexture> textures;
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

struct BackgroundCache {
    std::string key;
    std::vector<std::filesystem::path> frames;
    std::unique_ptr<Texture> texture;
    int frameIndex = -1;
};

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
    return !extensionMayHavePerFileIcon(pathExtensionKey(item.target));
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
    return std::filesystem::is_directory(source, ec) ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;
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
        return SHGetFileInfoW(source.wstring().c_str(), attributes, &info, sizeof(info), flags) == 0 ? nullptr : info.hIcon;
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
        return FAILED(imageList->GetIcon(iconIndex, ILD_TRANSPARENT, &icon)) ? nullptr : icon;
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
        return iconFromImageList(SHIL_LARGE, info.iIcon);
    };

    if (HICON icon = iconFromSystemImageList(exists ? 0 : SHGFI_USEFILEATTRIBUTES)) {
        return icon;
    }
    return exists ? iconFromSystemImageList(SHGFI_USEFILEATTRIBUTES) : nullptr;
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

    bool valid() const { return right >= left && bottom >= top; }
    int width() const { return right - left + 1; }
    int height() const { return bottom - top + 1; }
};

IconBitmapSize bitmapSizeForIcon(HICON icon)
{
    ICONINFO info{};
    if (!GetIconInfo(icon, &info)) {
        return {};
    }

    BITMAP bitmap{};
    IconBitmapSize result{};
    if (info.hbmColor && GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
        result.width = bitmap.bmWidth;
        result.height = bitmap.bmHeight;
    } else if (info.hbmMask && GetObjectW(info.hbmMask, sizeof(bitmap), &bitmap) == sizeof(bitmap)) {
        result.width = bitmap.bmWidth;
        result.height = bitmap.bmHeight / 2;
    }
    if (info.hbmColor) {
        DeleteObject(info.hbmColor);
    }
    if (info.hbmMask) {
        DeleteObject(info.hbmMask);
    }

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
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        return {};
    }

    HGDIOBJ old = SelectObject(dc, bitmap);
    std::fill_n(static_cast<std::uint32_t*>(bits), width * height, 0);
    DrawIconEx(dc, 0, 0, icon, width, height, 0, nullptr, DI_NORMAL);
    SelectObject(dc, old);

    std::vector<std::uint32_t> rgba(static_cast<size_t>(width) * height);
    const auto* bgra = static_cast<const std::uint8_t*>(bits);
    for (int i = 0; i < width * height; ++i) {
        const std::uint8_t b = bgra[i * 4];
        const std::uint8_t g = bgra[i * 4 + 1];
        const std::uint8_t r = bgra[i * 4 + 2];
        std::uint8_t a = bgra[i * 4 + 3];
        if (a == 0 && (r != 0 || g != 0 || b != 0)) {
            a = 255;
        }
        rgba[i] = (static_cast<std::uint32_t>(a) << 24) | (static_cast<std::uint32_t>(b) << 16) |
                  (static_cast<std::uint32_t>(g) << 8) | static_cast<std::uint32_t>(r);
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
            if (static_cast<std::uint8_t>(rgba[static_cast<size_t>(y) * width + x] >> 24) <= 8) {
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
    const bool offCenter = std::abs(contentCenterX - sourceCenterX) > sourceMax * 0.12f ||
                           std::abs(contentCenterY - sourceCenterY) > sourceMax * 0.12f;
    const bool anchoredTopLeft = bounds.left <= width * 0.04f && bounds.top <= height * 0.04f &&
                                 (bounds.right < width * 0.70f || bounds.bottom < height * 0.70f);
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
    const std::uint32_t samples[4] = {rgba[static_cast<size_t>(y0) * width + x0], rgba[static_cast<size_t>(y0) * width + x1],
                                      rgba[static_cast<size_t>(y1) * width + x0], rgba[static_cast<size_t>(y1) * width + x1]};
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
        const float scale = std::min(static_cast<float>(maxDraw) / srcW, static_cast<float>(maxDraw) / srcH);
        dstW = std::clamp(static_cast<int>(std::lround(srcW * scale)), 1, targetSize);
        dstH = std::clamp(static_cast<int>(std::lround(srcH * scale)), 1, targetSize);
        dstX = (targetSize - dstW) / 2;
        dstY = (targetSize - dstH) / 2;
    }
    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dstW; ++x) {
            const float srcX = static_cast<float>(sourceRect.left) + (static_cast<float>(x) + 0.5f) * srcW / dstW - 0.5f;
            const float srcY = static_cast<float>(sourceRect.top) + (static_cast<float>(y) + 0.5f) * srcH / dstH - 0.5f;
            target[static_cast<size_t>(dstY + y) * targetSize + dstX + x] = sampleIconPixel(source, sourceW, sourceH, srcX, srcY);
        }
    }
    return target;
}

std::unique_ptr<Texture> createTextureFromIcon(ID3D11Device* device, HICON icon)
{
    if (!device || !icon) {
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
    if (FAILED(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()))) {
        return {};
    }

    auto result = std::make_unique<Texture>();
    result->width = iconSize;
    result->height = iconSize;
    return FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, result->srv.GetAddressOf())) ? nullptr : std::move(result);
}

std::unique_ptr<Texture> createTextureFromImageFile(ID3D11Device* device, const std::filesystem::path& path)
{
    if (!device || path.empty()) {
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
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if (FAILED(decoder->GetFrame(0, frame.GetAddressOf())) || FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.0,
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
    if (FAILED(device->CreateTexture2D(&desc, &data, texture.GetAddressOf()))) {
        return {};
    }

    auto result = std::make_unique<Texture>();
    result->width = static_cast<int>(width);
    result->height = static_cast<int>(height);
    return FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, result->srv.GetAddressOf())) ? nullptr : std::move(result);
}

size_t textureByteSize(const Texture* texture)
{
    return !texture || texture->width <= 0 || texture->height <= 0
               ? 0
               : static_cast<size_t>(texture->width) * static_cast<size_t>(texture->height) * 4;
}

void trimCurrentProcessWorkingSet()
{
    SetProcessWorkingSetSize(GetCurrentProcess(), static_cast<SIZE_T>(-1), static_cast<SIZE_T>(-1));
}

} // namespace

struct MainDockResources::Impl {
    ID3D11Device* device = nullptr;
    IconCache icons;
    BackgroundCache background;

    void clearIcons()
    {
        icons.textures.clear();
        icons.pendingRequests.clear();
        icons.pendingRequestKeys.clear();
        icons.failedRequestFrame.clear();
        icons.totalBytes = 0;
        icons.workingSetTrimPending = false;
    }

    void resetIconScheduling()
    {
        icons.loadScopeKey.clear();
        icons.pendingRequests.clear();
        icons.pendingRequestKeys.clear();
        icons.failedRequestFrame.clear();
        icons.loadPauseFrames = 0;
        icons.scrollPauseFrames = 0;
        icons.loadBudgetThisFrame = kMaxIconTextureLoadsPerFrame;
        icons.workingSetTrimPending = false;
        icons.lastIconLoadFrame = 0;
    }

    void clearBackground()
    {
        background.key.clear();
        background.frames.clear();
        background.texture.reset();
        background.frameIndex = -1;
    }

    void eraseIcon(const std::string& key)
    {
        auto it = icons.textures.find(key);
        if (it == icons.textures.end()) {
            return;
        }
        icons.totalBytes -= std::min(icons.totalBytes, it->second.byteSize);
        icons.textures.erase(it);
    }

    void markWorkingSetDirty()
    {
        icons.workingSetTrimPending = true;
        icons.lastIconLoadFrame = ImGui::GetFrameCount();
    }

    void maybeTrimWorkingSet()
    {
        if (!icons.workingSetTrimPending || !icons.pendingRequests.empty()) {
            return;
        }
        const int frame = ImGui::GetFrameCount();
        if (frame - icons.lastIconLoadFrame < kIconWorkingSetTrimIdleFrames ||
            frame - icons.lastWorkingSetTrimFrame < kIconWorkingSetTrimMinIntervalFrames) {
            return;
        }
        trimCurrentProcessWorkingSet();
        icons.workingSetTrimPending = false;
        icons.lastWorkingSetTrimFrame = frame;
    }

    void pruneIcons()
    {
        const int currentFrame = ImGui::GetFrameCount();
        while ((icons.totalBytes > kMaxIconTextureCacheBytes || icons.textures.size() > kMaxIconTextureCacheEntries) &&
               !icons.textures.empty()) {
            auto candidate = icons.textures.end();
            for (auto it = icons.textures.begin(); it != icons.textures.end(); ++it) {
                if (it->second.lastUsedFrame == currentFrame && icons.textures.size() <= kMaxIconTextureCacheEntries) {
                    continue;
                }
                if (candidate == icons.textures.end() || it->second.lastUsedFrame < candidate->second.lastUsedFrame) {
                    candidate = it;
                }
            }
            if (candidate == icons.textures.end()) {
                return;
            }
            icons.totalBytes -= std::min(icons.totalBytes, candidate->second.byteSize);
            icons.textures.erase(candidate);
        }
    }

    Texture* iconForItem(const LaunchItem& item, bool useDefaultIcons)
    {
        if (useDefaultIcons || !device || item.type == LaunchItemType::Placeholder || item.type == LaunchItemType::Title) {
            return nullptr;
        }
        const std::string key = iconCacheKey(item);
        const int frame = ImGui::GetFrameCount();
        if (auto it = icons.textures.find(key); it != icons.textures.end()) {
            it->second.lastUsedFrame = frame;
            return it->second.texture.get();
        }
        if (icons.loadBudgetThisFrame <= 0) {
            return nullptr;
        }
        --icons.loadBudgetThisFrame;

        HICON icon = shellIconForItem(item);
        markWorkingSetDirty();
        std::unique_ptr<Texture> texture = createTextureFromIcon(device, icon);
        if (icon) {
            DestroyIcon(icon);
        }
        if (!texture || !texture->srv) {
            return nullptr;
        }

        CachedTexture entry;
        entry.byteSize = textureByteSize(texture.get());
        entry.lastUsedFrame = frame;
        entry.texture = std::move(texture);
        Texture* result = entry.texture.get();
        icons.totalBytes += entry.byteSize;
        icons.textures.emplace(key, std::move(entry));
        pruneIcons();
        return result;
    }

    std::unique_ptr<Texture> loadBackgroundTexture(const std::filesystem::path& path) const
    {
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            return {};
        }
        try {
            return createTextureFromImageFile(device, path);
        } catch (...) {
            return {};
        }
    }

    Texture* backgroundForTheme(const AppContext& context, const ThemeDefinition& theme)
    {
        if (!theme.background.enabled || theme.background.imagePath.empty() || !device) {
            return nullptr;
        }
        if (!theme.background.animated) {
            const std::string key = "static:" + theme.background.imagePath.string();
            if (background.key != key) {
                clearBackground();
                background.key = key;
                background.texture = loadBackgroundTexture(theme.background.imagePath);
            }
            return background.texture.get();
        }

        const int fps = std::clamp(theme.background.animationFps, 1, 30);
        const int maxWidth = std::clamp(theme.background.animationMaxWidth, 240, 3840);
        const int quality = std::clamp(theme.background.animationQuality, 2, 31);
        const std::string cacheKey = animatedBackgroundCacheKey(theme.background.imagePath, fps, maxWidth, quality);
        const std::string key = "animated:" + cacheKey;
        if (background.key != key) {
            clearBackground();
            background.key = key;
            const std::filesystem::path root = context.config.directory() / "background-cache";
            background.frames = animatedBackgroundFrames(animatedBackgroundCacheDirectory(root, cacheKey));
        }
        if (background.frames.empty()) {
            return nullptr;
        }

        const int frameCount = static_cast<int>(background.frames.size());
        HWND hwnd = mainWindowHandle();
        const bool foreground = hwnd && GetForegroundWindow() == hwnd;
        const int frame = foreground || background.frameIndex < 0
                              ? static_cast<int>(ImGui::GetTime() * static_cast<double>(fps)) % frameCount
                              : background.frameIndex;
        if (background.frameIndex != frame) {
            background.texture = loadBackgroundTexture(background.frames[static_cast<size_t>(frame)]);
            background.frameIndex = frame;
        }
        return background.texture.get();
    }
};

MainDockResources::MainDockResources() : impl_(std::make_unique<Impl>()) {}
MainDockResources::~MainDockResources() = default;

void MainDockResources::setDevice(ID3D11Device* device)
{
    if (impl_->device != device) {
        clear();
        resetIconLoadScheduling();
    }
    impl_->device = device;
}

void MainDockResources::clear()
{
    impl_->clearIcons();
    impl_->clearBackground();
}

void MainDockResources::clearIcons(bool trimWorkingSet)
{
    impl_->clearIcons();
    if (trimWorkingSet) {
        trimCurrentProcessWorkingSet();
    }
}

void MainDockResources::clearBackground()
{
    impl_->clearBackground();
}

void MainDockResources::resetIconLoadScheduling()
{
    impl_->resetIconScheduling();
}

void MainDockResources::beginIconLoadFrame(const AppContext& context, bool searchOpen, bool useDefaultIcons,
                                           const char* searchQueryText)
{
    IconCache& cache = impl_->icons;
    if (useDefaultIcons) {
        if (!cache.textures.empty() || !cache.pendingRequests.empty()) {
            impl_->clearIcons();
            trimCurrentProcessWorkingSet();
        }
        cache.loadScopeKey = "default-icons";
        cache.loadBudgetThisFrame = 0;
        return;
    }

    std::string scopeKey;
    if (searchOpen) {
        scopeKey = "search:" + context.runtime().searchText + "|" + (searchQueryText ? searchQueryText : "");
    } else {
        scopeKey = "category:" + std::to_string(context.runtime().selectedCategory);
        if (context.runtime().selectedCategory >= 0 &&
            context.runtime().selectedCategory < static_cast<int>(context.persisted().categories.size())) {
            scopeKey += ":" + context.persisted().categories[context.runtime().selectedCategory].id;
        }
        for (const std::string& folderId : context.runtime().currentFolderStack) {
            scopeKey += "/" + folderId;
        }
    }

    if (scopeKey != cache.loadScopeKey) {
        const bool hadCachedIcons = !cache.textures.empty();
        impl_->clearIcons();
        if (hadCachedIcons) {
            trimCurrentProcessWorkingSet();
        }
        cache.loadScopeKey = std::move(scopeKey);
        cache.loadPauseFrames = searchOpen ? kSearchIconLoadPauseFramesAfterScopeChange : kIconLoadPauseFramesAfterScopeChange;
    }
    if (cache.loadPauseFrames > 0) {
        --cache.loadPauseFrames;
        cache.loadBudgetThisFrame = 0;
        return;
    }
    if (searchOpen && std::abs(ImGui::GetIO().MouseWheel) > 0.0f) {
        cache.scrollPauseFrames = kIconLoadPauseFramesAfterScroll;
    }
    if (cache.scrollPauseFrames > 0) {
        --cache.scrollPauseFrames;
        cache.loadBudgetThisFrame = 0;
        return;
    }
    cache.loadBudgetThisFrame = searchOpen ? kMaxSearchIconTextureLoadsPerFrame : kMaxIconTextureLoadsPerFrame;
}

void MainDockResources::processPendingIconRequests(bool useDefaultIcons)
{
    IconCache& cache = impl_->icons;
    if (useDefaultIcons) {
        cache.pendingRequests.clear();
        cache.pendingRequestKeys.clear();
        impl_->maybeTrimWorkingSet();
        return;
    }
    if (cache.pendingRequests.empty()) {
        impl_->maybeTrimWorkingSet();
        return;
    }
    if (cache.loadBudgetThisFrame <= 0) {
        return;
    }
    while (!cache.pendingRequests.empty()) {
        LaunchItem item = std::move(cache.pendingRequests.front());
        cache.pendingRequests.pop_front();
        const std::string key = iconCacheKey(item);
        cache.pendingRequestKeys.erase(key);
        if (cache.textures.contains(key)) {
            continue;
        }
        if (cache.loadBudgetThisFrame <= 0) {
            requestLaunchIcon(item, useDefaultIcons);
            return;
        }
        if (!impl_->iconForItem(item, useDefaultIcons)) {
            cache.failedRequestFrame[key] = ImGui::GetFrameCount();
        }
        if (cache.loadBudgetThisFrame <= 0) {
            return;
        }
    }
    impl_->maybeTrimWorkingSet();
}

void MainDockResources::requestLaunchIcon(const LaunchItem& item, bool useDefaultIcons)
{
    IconCache& cache = impl_->icons;
    if (useDefaultIcons || !impl_->device || item.type == LaunchItemType::Placeholder || item.type == LaunchItemType::Title) {
        return;
    }
    const std::string key = iconCacheKey(item);
    if (cache.textures.contains(key) || cache.pendingRequestKeys.contains(key)) {
        return;
    }
    if (auto failed = cache.failedRequestFrame.find(key); failed != cache.failedRequestFrame.end()) {
        if (ImGui::GetFrameCount() - failed->second < kFailedIconRetryFrames) {
            return;
        }
        cache.failedRequestFrame.erase(failed);
    }
    while (cache.pendingRequests.size() >= kMaxPendingIconRequests) {
        cache.pendingRequestKeys.erase(iconCacheKey(cache.pendingRequests.front()));
        cache.pendingRequests.pop_front();
    }
    cache.pendingRequests.push_back(item);
    cache.pendingRequestKeys.insert(key);
}

void MainDockResources::clearIconForItem(const LaunchItem& item)
{
    impl_->eraseIcon(iconCacheKey(item));
}

bool MainDockResources::drawLaunchIcon(ImDrawList* drawList, const LaunchItem& item, const ImVec2& pos, float size,
                                       bool useDefaultIcons)
{
    Texture* texture = impl_->iconForItem(item, useDefaultIcons);
    if (!texture || !texture->srv) {
        return false;
    }
    const float rounding = item.type == LaunchItemType::Url ? size * 0.5f : 8.0f;
    drawList->AddImageRounded(static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->srv.Get())), pos,
                              ImVec2(pos.x + size, pos.y + size), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
                              IM_COL32_WHITE, rounding);
    return true;
}

bool MainDockResources::drawCachedLaunchIcon(const LaunchItem& item, const ImVec2& pos, float size, bool useDefaultIcons)
{
    if (useDefaultIcons) {
        return false;
    }
    const std::string key = iconCacheKey(item);
    auto it = impl_->icons.textures.find(key);
    if (it == impl_->icons.textures.end() || !it->second.texture || !it->second.texture->srv) {
        return false;
    }
    it->second.lastUsedFrame = ImGui::GetFrameCount();
    const float rounding = item.type == LaunchItemType::Url ? size * 0.5f : 8.0f;
    ImGui::GetWindowDrawList()->AddImageRounded(
        static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(it->second.texture->srv.Get())), pos,
        ImVec2(pos.x + size, pos.y + size), ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), IM_COL32_WHITE, rounding);
    return true;
}

bool MainDockResources::hasBackground(const ThemeDefinition& theme) const
{
    return theme.background.enabled && !theme.background.imagePath.empty();
}

void MainDockResources::drawBackground(const AppContext& context, const ThemeDefinition& theme, const ImVec2& origin,
                                       const ImVec2& size)
{
    Texture* texture = impl_->backgroundForTheme(context, theme);
    if (!texture || !texture->srv || texture->width <= 0 || texture->height <= 0) {
        return;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImTextureID id = static_cast<ImTextureID>(reinterpret_cast<std::uintptr_t>(texture->srv.Get()));
    const ImU32 tint = IM_COL32(255, 255, 255, std::clamp(theme.background.opacity * 255 / 100, 0, 255));
    const float imageW = static_cast<float>(texture->width);
    const float imageH = static_cast<float>(texture->height);
    const float areaW = std::max(1.0f, size.x);
    const float areaH = std::max(1.0f, size.y);
    ImVec2 min = origin;
    ImVec2 max(origin.x + areaW, origin.y + areaH);
    if (theme.background.imageMode == 3) {
        drawList->PushClipRect(min, max, true);
        for (float y = min.y; y < max.y; y += imageH) {
            for (float x = min.x; x < max.x; x += imageW) {
                drawList->AddImage(id, ImVec2(x, y), ImVec2(x + imageW, y + imageH), ImVec2(0.0f, 0.0f),
                                   ImVec2(1.0f, 1.0f), tint);
            }
        }
        drawList->PopClipRect();
        return;
    }

    float drawW = imageW;
    float drawH = imageH;
    if (theme.background.imageMode == 0 || theme.background.imageMode == 1) {
        const float scale = theme.background.imageMode == 0 ? std::max(areaW / imageW, areaH / imageH)
                                                            : std::min(areaW / imageW, areaH / imageH);
        drawW = imageW * scale;
        drawH = imageH * scale;
    } else if (theme.background.imageMode == 2) {
        drawW = areaW;
        drawH = areaH;
    }
    min = ImVec2(origin.x + (areaW - drawW) * 0.5f, origin.y + (areaH - drawH) * 0.5f);
    max = ImVec2(min.x + drawW, min.y + drawH);
    drawList->PushClipRect(origin, ImVec2(origin.x + areaW, origin.y + areaH), true);
    drawList->AddImageRounded(id, min, max, ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f), tint, uiPalette(theme).windowRounding);
    drawList->PopClipRect();
}

} // namespace launcher
