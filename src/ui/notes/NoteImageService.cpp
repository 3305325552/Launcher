#include "ui/notes/NoteImageService.hpp"

#include "core/NotesStore.hpp"
#include "core/StringEncoding.hpp"
#include "ui/common/Localization.hpp"

#include <windows.h>
#include <commdlg.h>
#include <imgui.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <utility>

namespace launcher {
namespace {

using Microsoft::WRL::ComPtr;

void setError(std::string* error, std::string message)
{
    if (error) {
        *error = std::move(message);
    }
}

HWND dialogOwner()
{
    if (ImGuiViewport* viewport = ImGui::GetWindowViewport()) {
        return static_cast<HWND>(viewport->PlatformHandleRaw);
    }
    return nullptr;
}

std::filesystem::path openImageFileDialog()
{
    wchar_t file[MAX_PATH]{};
    const std::wstring title = trw("Select Note Image");
    const std::wstring filter = fileDialogFilter({{"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff"}, {"All Files", L"*.*"}});
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = dialogOwner();
    ofn.lpstrTitle = title.c_str();
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_EXPLORER | OFN_ENABLESIZING | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    return GetOpenFileNameW(&ofn) ? std::filesystem::path(file) : std::filesystem::path{};
}

ComPtr<IWICImagingFactory> createWicFactory()
{
    ComPtr<IWICImagingFactory> factory;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(factory.GetAddressOf()));
    return factory;
}

bool decodableImage(const std::filesystem::path& path)
{
    ComPtr<IWICImagingFactory> factory = createWicFactory();
    ComPtr<IWICBitmapDecoder> decoder;
    return factory && SUCCEEDED(factory->CreateDecoderFromFilename(path.wstring().c_str(), nullptr, GENERIC_READ,
                                                                   WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()));
}

std::string markdownAlt(std::string value)
{
    std::replace(value.begin(), value.end(), '\n', ' ');
    std::replace(value.begin(), value.end(), '\r', ' ');
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        if (ch == '\\' || ch == '[' || ch == ']') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped.empty() ? std::string(tr("Image")) : escaped;
}

NoteImageInsert makeInsert(const std::filesystem::path& path, const std::string& alt)
{
    NoteImageInsert result;
    result.path = path;
    result.markdown = "![" + markdownAlt(alt) + "](attachment:" + pathToUtf8(path.filename()) + ")";
    return result;
}

bool writeClipboardBitmap(const std::filesystem::path& path, std::string* error)
{
    if (!OpenClipboard(dialogOwner())) {
        setError(error, "could not open the clipboard");
        return false;
    }
    HBITMAP handle = static_cast<HBITMAP>(GetClipboardData(CF_BITMAP));
    ComPtr<IWICImagingFactory> factory = createWicFactory();
    ComPtr<IWICBitmap> bitmap;
    const HRESULT bitmapResult =
        handle && factory ? factory->CreateBitmapFromHBITMAP(handle, nullptr, WICBitmapIgnoreAlpha, bitmap.GetAddressOf()) : E_FAIL;
    CloseClipboard();
    if (FAILED(bitmapResult)) {
        setError(error, "the clipboard does not contain a readable image");
        return false;
    }

    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> encoder;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    UINT width = 0;
    UINT height = 0;
    HRESULT hr = factory->CreateStream(stream.GetAddressOf());
    if (SUCCEEDED(hr)) hr = stream->InitializeFromFilename(path.wstring().c_str(), GENERIC_WRITE);
    if (SUCCEEDED(hr)) hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf());
    if (SUCCEEDED(hr)) hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    if (SUCCEEDED(hr)) hr = encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf());
    if (SUCCEEDED(hr)) hr = frame->Initialize(properties.Get());
    if (SUCCEEDED(hr)) hr = bitmap->GetSize(&width, &height);
    if (SUCCEEDED(hr)) hr = frame->SetSize(width, height);
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (SUCCEEDED(hr)) hr = frame->SetPixelFormat(&format);
    if (SUCCEEDED(hr)) hr = frame->WriteSource(bitmap.Get(), nullptr);
    if (SUCCEEDED(hr)) hr = frame->Commit();
    if (SUCCEEDED(hr)) hr = encoder->Commit();
    if (FAILED(hr)) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        setError(error, "could not save the clipboard image");
        return false;
    }
    return true;
}

} // namespace

bool noteImageAvailableOnClipboard()
{
    return IsClipboardFormatAvailable(CF_BITMAP) != FALSE;
}

std::optional<NoteImageInsert> chooseNoteImage(NotesStore& notes, const Note& note, std::string* error)
{
    const std::filesystem::path source = openImageFileDialog();
    if (source.empty()) {
        return std::nullopt;
    }
    if (!decodableImage(source)) {
        setError(error, "unsupported or unreadable image");
        return std::nullopt;
    }
    const std::filesystem::path imported = notes.importAttachment(note.id, source, error);
    return imported.empty() ? std::nullopt : std::optional<NoteImageInsert>(makeInsert(imported, pathToUtf8(source.stem())));
}

std::optional<NoteImageInsert> pasteNoteImage(NotesStore& notes, const Note& note, std::string* error)
{
    const std::filesystem::path path = notes.createAttachmentPath(note.id, "clipboard.png", error);
    if (path.empty() || !writeClipboardBitmap(path, error)) {
        return std::nullopt;
    }
    return makeInsert(path, tr("Clipboard Image"));
}

} // namespace launcher
