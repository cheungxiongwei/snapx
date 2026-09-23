#include "encode.h"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cmath>

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

}

bool EncodeToFile(const CaptureResult& image, const std::wstring& path,
                  ImageFormat format, int jpegQuality, double scale, std::wstring& error) {
    if (image.width == 0 || image.height == 0 || image.pixels.empty()) {
        error = L"No image data to encode.";
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

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(factory.GetAddressOf()));
    if (FAILED(hr)) {
        error = L"Failed to create the WIC imaging factory.";
        return false;
    }

    ComPtr<IWICStream> stream;
    hr = factory->CreateStream(stream.GetAddressOf());
    if (SUCCEEDED(hr)) {
        hr = stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE);
    }
    if (FAILED(hr)) {
        error = L"Cannot write '" + path + L"'";
        return false;
    }

    ComPtr<IWICBitmapEncoder> encoder;
    if (SUCCEEDED(hr)) {
        hr = factory->CreateEncoder(ContainerForFormat(format), nullptr,
                                    encoder.GetAddressOf());
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    }
    if (FAILED(hr)) {
        error = L"Failed to initialize the " + FormatName(format) + L" encoder.";
        return false;
    }

    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> properties;
    if (SUCCEEDED(hr)) {
        hr = encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf());
    }
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

    if (SUCCEEDED(hr)) {
        hr = frame->Commit();
    }
    if (SUCCEEDED(hr)) {
        hr = encoder->Commit();
    }
    if (FAILED(hr)) {
        error = L"Failed to finalize output file: " + path;
        return false;
    }

    return true;
}

}
