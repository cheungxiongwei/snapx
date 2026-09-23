#include "clipboard.h"
#include "encode.h"

#include <windows.h>
#include <cstring>
#include <vector>

namespace snapx {

namespace {

bool OpenClipboardWithRetry() {
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (OpenClipboard(nullptr)) {
            return true;
        }
        Sleep(10);
    }
    return false;
}

bool SetClipboardBlob(UINT format, const void* data, size_t size) {
    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, size);
    if (handle == nullptr) {
        return false;
    }
    void* dest = GlobalLock(handle);
    if (dest == nullptr) {
        GlobalFree(handle);
        return false;
    }
    std::memcpy(dest, data, size);
    GlobalUnlock(handle);
    if (SetClipboardData(format, handle) == nullptr) {
        GlobalFree(handle);
        return false;
    }
    return true;
}

bool SetClipboardDib(const uint8_t* bgra, uint32_t width, uint32_t height) {
    const size_t rowPitch = static_cast<size_t>(width) * 4;
    const size_t imageSize = rowPitch * height;
    const size_t total = sizeof(BITMAPINFOHEADER) + imageSize;

    HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, total);
    if (handle == nullptr) {
        return false;
    }
    auto* base = static_cast<uint8_t*>(GlobalLock(handle));
    if (base == nullptr) {
        GlobalFree(handle);
        return false;
    }

    BITMAPINFOHEADER header{};
    header.biSize = sizeof(BITMAPINFOHEADER);
    header.biWidth = static_cast<LONG>(width);
    header.biHeight = static_cast<LONG>(height);
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;
    header.biSizeImage = static_cast<DWORD>(imageSize);
    std::memcpy(base, &header, sizeof(header));

    for (uint32_t y = 0; y < height; ++y) {
        std::memcpy(base + sizeof(BITMAPINFOHEADER) +
                        static_cast<size_t>(height - 1 - y) * rowPitch,
                    bgra + static_cast<size_t>(y) * rowPitch,
                    rowPitch);
    }
    GlobalUnlock(handle);

    if (SetClipboardData(CF_DIB, handle) == nullptr) {
        GlobalFree(handle);
        return false;
    }
    return true;
}

}

bool CopyImageToClipboard(const CaptureResult& image, double scale, std::wstring& error) {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> bgra;
    if (!ScaleToBgra(image, scale, width, height, bgra, error)) {
        return false;
    }

    std::vector<uint8_t> png;
    if (!EncodeToMemory(image, ImageFormat::Png, 90, scale, png, error)) {
        return false;
    }

    if (!OpenClipboardWithRetry()) {
        error = L"Cannot open the clipboard.";
        return false;
    }
    if (!EmptyClipboard()) {
        CloseClipboard();
        error = L"Cannot empty the clipboard.";
        return false;
    }

    const bool dibOk = SetClipboardDib(bgra.data(), width, height);
    const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    const bool pngOk = pngFormat != 0 && SetClipboardBlob(pngFormat, png.data(), png.size());
    CloseClipboard();

    if (!dibOk && !pngOk) {
        error = L"Cannot write the image to the clipboard.";
        return false;
    }
    return true;
}

}
