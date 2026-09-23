#include "encode.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>
#include <cstring>
#include <vector>

namespace snapx {

namespace {

using Microsoft::WRL::ComPtr;

const GUID& ContainerForFormat(ImageFormat format) {
    switch (format) {
    case ImageFormat::Png:
        return GUID_ContainerFormatPng;
    case ImageFormat::Jpeg:
        return GUID_ContainerFormatJpeg;
    case ImageFormat::Bmp:
        return GUID_ContainerFormatBmp;
    }
    return GUID_ContainerFormatPng;
}

std::wstring FormatName(ImageFormat format) {
    switch (format) {
    case ImageFormat::Png:
        return L"PNG";
    case ImageFormat::Jpeg:
        return L"JPEG";
    case ImageFormat::Bmp:
        return L"BMP";
    }
    return L"PNG";
}

bool IsEmpty(const CaptureResult& image) {
    return image.width == 0 || image.height == 0 || image.pixels.empty();
}

ComPtr<IWICImagingFactory> CreateFactory(std::wstring& error) {
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        error = L"Failed to create the WIC imaging factory.";
    }
    return factory;
}

bool EncodeCore(IWICImagingFactory* factory, IWICStream* stream, const CaptureResult& image,
                ImageFormat format, int jpegQuality, double scale, std::wstring& error) {
    const uint32_t outWidth = static_cast<uint32_t>(
        std::lround(static_cast<double>(image.width) * scale));
    const uint32_t outHeight = static_cast<uint32_t>(
        std::lround(static_cast<double>(image.height) * scale));
    if (outWidth == 0 || outHeight == 0) {
        error = L"--scale produces a zero-sized image";
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    HRESULT hr = factory->CreateEncoder(ContainerForFormat(format), nullptr,
                                        encoder.GetAddressOf());
    if (SUCCEEDED(hr)) {
        hr = encoder->Initialize(stream, WICBitmapEncoderNoCache);
    }
    if (FAILED(hr)) {
        error = L"Failed to initialize the " + FormatName(format) + L" encoder.";
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    hr = encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf());
    if (SUCCEEDED(hr) && format == ImageFormat::Jpeg && properties != nullptr) {
        PROPBAG2 option = {};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = static_cast<float>(jpegQuality) / 100.0f;
        properties->Write(1, &option, &value);
        VariantClear(&value);
    }
    if (SUCCEEDED(hr)) {
        hr = frame->Initialize(properties.Get());
    }
    if (SUCCEEDED(hr)) {
        hr = frame->SetSize(outWidth, outHeight);
    }
    if (FAILED(hr)) {
        error = L"Failed to configure the encoder frame.";
        return false;
    }

    ComPtr<IWICBitmap> bitmap;
    hr = factory->CreateBitmapFromMemory(
        image.width,
        image.height,
        GUID_WICPixelFormat32bppBGRA,
        image.rowPitch,
        static_cast<UINT>(image.pixels.size()),
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(image.pixels.data())),
        bitmap.GetAddressOf());
    if (FAILED(hr)) {
        error = L"Failed to create a WIC bitmap from the captured pixels.";
        return false;
    }

    ComPtr<IWICBitmapSource> source = bitmap;
    ComPtr<IWICBitmapScaler> scaler;
    if (outWidth != image.width || outHeight != image.height) {
        hr = factory->CreateBitmapScaler(scaler.GetAddressOf());
        if (SUCCEEDED(hr)) {
            hr = scaler->Initialize(bitmap.Get(), outWidth, outHeight,
                                    WICBitmapInterpolationModeFant);
        }
        if (FAILED(hr)) {
            error = L"Failed to scale the captured image.";
            return false;
        }
        source = scaler;
    }

    WICPixelFormatGUID targetFormat = GUID_WICPixelFormat32bppBGRA;
    {
        WICPixelFormatGUID probe = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(frame->SetPixelFormat(&probe))) {
            targetFormat = probe;
        }
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(converter.GetAddressOf());
    if (SUCCEEDED(hr)) {
        hr = converter->Initialize(source.Get(), targetFormat, WICBitmapDitherTypeNone,
                                   nullptr, 0.0, WICBitmapPaletteTypeCustom);
    }
    if (FAILED(hr)) {
        error = L"Failed to convert pixels to the encoder's pixel format.";
        return false;
    }

    hr = frame->WriteSource(converter.Get(), nullptr);
    if (FAILED(hr)) {
        error = L"Failed to write pixel data.";
        return false;
    }

    hr = frame->Commit();
    if (SUCCEEDED(hr)) {
        hr = encoder->Commit();
    }
    if (FAILED(hr)) {
        error = L"Failed to finalize the encoded image.";
        return false;
    }

    return true;
}

}

bool EncodeToFile(const CaptureResult& image, const std::wstring& path,
                  ImageFormat format, int jpegQuality, double scale, std::wstring& error) {
    if (IsEmpty(image)) {
        error = L"No image data to encode.";
        return false;
    }

    ComPtr<IWICImagingFactory> factory = CreateFactory(error);
    if (factory == nullptr) {
        return false;
    }

    ComPtr<IWICStream> stream;
    HRESULT hr = factory->CreateStream(stream.GetAddressOf());
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    }
    if (FAILED(hr)) {
        error = L"Cannot write '" + path + L"'";
        return false;
    }

    if (!EncodeCore(factory.Get(), stream.Get(), image, format, jpegQuality, scale, error)) {
        return false;
    }
    return true;
}

bool EncodeToMemory(const CaptureResult& image, ImageFormat format, int jpegQuality,
                    double scale, std::vector<uint8_t>& bytes, std::wstring& error) {
    bytes.clear();
    if (IsEmpty(image)) {
        error = L"No image data to encode.";
        return false;
    }

    ComPtr<IWICImagingFactory> factory = CreateFactory(error);
    if (factory == nullptr) {
        return false;
    }

    ComPtr<IStream> memoryStream;
    HRESULT hr = CreateStreamOnHGlobal(nullptr, TRUE, memoryStream.GetAddressOf());
    if (FAILED(hr)) {
        error = L"Failed to create an in-memory stream.";
        return false;
    }

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(stream.GetAddressOf());
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromIStream(memoryStream.Get());
    }
    if (FAILED(hr)) {
        error = L"Failed to initialize the encoder stream.";
        return false;
    }

    if (!EncodeCore(factory.Get(), stream.Get(), image, format, jpegQuality, scale, error)) {
        return false;
    }
    stream.Reset();

    HGLOBAL global = nullptr;
    hr = GetHGlobalFromStream(memoryStream.Get(), &global);
    if (FAILED(hr) || global == nullptr) {
        error = L"Failed to read the encoded image.";
        return false;
    }

    STATSTG stat{};
    hr = memoryStream->Stat(&stat, STATFLAG_NONAME);
    if (FAILED(hr)) {
        error = L"Failed to read the encoded image.";
        return false;
    }
    const size_t size = static_cast<size_t>(stat.cbSize.QuadPart);
    if (size == 0) {
        error = L"Encoded image is empty.";
        return false;
    }

    void* data = GlobalLock(global);
    if (data == nullptr) {
        error = L"Failed to read the encoded image.";
        return false;
    }
    bytes.assign(static_cast<const uint8_t*>(data),
                 static_cast<const uint8_t*>(data) + size);
    GlobalUnlock(global);
    return true;
}

bool ScaleToBgra(const CaptureResult& image, double scale, uint32_t& width, uint32_t& height,
                 std::vector<uint8_t>& pixels, std::wstring& error) {
    if (IsEmpty(image)) {
        error = L"No image data to scale.";
        return false;
    }

    const uint32_t outWidth = static_cast<uint32_t>(
        std::lround(static_cast<double>(image.width) * scale));
    const uint32_t outHeight = static_cast<uint32_t>(
        std::lround(static_cast<double>(image.height) * scale));
    if (outWidth == 0 || outHeight == 0) {
        error = L"--scale produces a zero-sized image";
        return false;
    }

    if (outWidth == image.width && outHeight == image.height) {
        width = image.width;
        height = image.height;
        pixels.assign(image.pixels.begin(), image.pixels.end());
        return true;
    }

    ComPtr<IWICImagingFactory> factory = CreateFactory(error);
    if (factory == nullptr) {
        return false;
    }

    ComPtr<IWICBitmap> bitmap;
    HRESULT hr = factory->CreateBitmapFromMemory(
        image.width,
        image.height,
        GUID_WICPixelFormat32bppBGRA,
        image.rowPitch,
        static_cast<UINT>(image.pixels.size()),
        const_cast<BYTE*>(reinterpret_cast<const BYTE*>(image.pixels.data())),
        bitmap.GetAddressOf());
    if (FAILED(hr)) {
        error = L"Failed to create a WIC bitmap from the captured pixels.";
        return false;
    }

    ComPtr<IWICBitmapScaler> scaler;
    hr = factory->CreateBitmapScaler(scaler.GetAddressOf());
    if (SUCCEEDED(hr)) {
        hr = scaler->Initialize(bitmap.Get(), outWidth, outHeight,
                                WICBitmapInterpolationModeFant);
    }
    if (FAILED(hr)) {
        error = L"Failed to scale the captured image.";
        return false;
    }

    const uint32_t rowPitch = outWidth * 4;
    pixels.resize(static_cast<size_t>(rowPitch) * outHeight);
    hr = scaler->CopyPixels(nullptr, rowPitch, static_cast<UINT>(pixels.size()),
                            pixels.data());
    if (FAILED(hr)) {
        error = L"Failed to read the scaled image.";
        return false;
    }

    width = outWidth;
    height = outHeight;
    return true;
}

}
