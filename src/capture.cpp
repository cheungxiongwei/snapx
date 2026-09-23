#include "capture.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <chrono>
#include <thread>
#include <vector>

namespace snapx {

namespace {

using namespace winrt::Windows::Graphics::Capture;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::DirectX::Direct3D11;

winrt::com_ptr<ID3D11Device> CreateD3DDevice() {
    winrt::com_ptr<ID3D11Device> device;
    const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };
    D3D_FEATURE_LEVEL achieved{};
    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        levels,
        ARRAYSIZE(levels),
        D3D11_SDK_VERSION,
        device.put(),
        &achieved,
        nullptr);
    if (FAILED(hr)) {
        winrt::check_hresult(hr);
    }
    return device;
}

IDirect3DDevice WrapDevice(const winrt::com_ptr<ID3D11Device>& device) {
    winrt::com_ptr<IDXGIDevice> dxgiDevice;
    winrt::check_hresult(device->QueryInterface(winrt::guid_of<IDXGIDevice>(),
                                                dxgiDevice.put_void()));
    winrt::com_ptr<IInspectable> inspectable;
    winrt::check_hresult(CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.get(),
                                                              inspectable.put()));
    return inspectable.as<IDirect3DDevice>();
}

GraphicsCaptureItem CreateItemForWindow(HWND hwnd) {
    auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{ nullptr };
    winrt::check_hresult(interop->CreateForWindow(
        hwnd,
        winrt::guid_of<GraphicsCaptureItem>(),
        winrt::put_abi(item)));
    return item;
}

GraphicsCaptureItem CreateItemForMonitor(HMONITOR monitor) {
    auto interop = winrt::get_activation_factory<GraphicsCaptureItem, IGraphicsCaptureItemInterop>();
    GraphicsCaptureItem item{ nullptr };
    winrt::check_hresult(interop->CreateForMonitor(
        monitor,
        winrt::guid_of<GraphicsCaptureItem>(),
        winrt::put_abi(item)));
    return item;
}

bool GrabItem(const GraphicsCaptureItem& item, uint32_t timeoutMs, CaptureResult& result,
              std::wstring& error) {
    const winrt::Windows::Graphics::SizeInt32 size = item.Size();
    if (size.Width <= 0 || size.Height <= 0) {
        error = L"Capture item has an invalid size.";
        return false;
    }

    winrt::com_ptr<ID3D11Device> d3dDevice = CreateD3DDevice();
    IDirect3DDevice winrtDevice = WrapDevice(d3dDevice);

    Direct3D11CaptureFramePool framePool = Direct3D11CaptureFramePool::CreateFreeThreaded(
        winrtDevice,
        DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,
        size);
    GraphicsCaptureSession session = framePool.CreateCaptureSession(item);
    session.StartCapture();

    Direct3D11CaptureFrame frame{ nullptr };
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        frame = framePool.TryGetNextFrame();
        if (frame != nullptr) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (frame == nullptr) {
        session.Close();
        framePool.Close();
        error = L"Timed out waiting for a capture frame.";
        return false;
    }

    const auto contentSize = frame.ContentSize();
    winrt::com_ptr<ID3D11Texture2D> source;
    {
        auto access = frame.Surface().as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
        winrt::check_hresult(access->GetInterface(winrt::guid_of<ID3D11Texture2D>(),
                                                  source.put_void()));
    }

    D3D11_TEXTURE2D_DESC desc{};
    source->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.BindFlags = 0;
    desc.MiscFlags = 0;

    winrt::com_ptr<ID3D11Texture2D> staging;
    winrt::check_hresult(d3dDevice->CreateTexture2D(&desc, nullptr, staging.put()));

    winrt::com_ptr<ID3D11DeviceContext> context;
    d3dDevice->GetImmediateContext(context.put());
    context->CopyResource(staging.get(), source.get());

    D3D11_MAPPED_SUBRESOURCE mapped{};
    winrt::check_hresult(context->Map(staging.get(), 0, D3D11_MAP_READ, 0, &mapped));

    const uint32_t width = static_cast<uint32_t>(contentSize.Width);
    const uint32_t height = static_cast<uint32_t>(contentSize.Height);
    result.width = width;
    result.height = height;
    result.rowPitch = width * 4;
    result.pixels.resize(static_cast<size_t>(result.rowPitch) * height);

    const auto* src = static_cast<const uint8_t*>(mapped.pData);
    for (uint32_t y = 0; y < height; ++y) {
        std::memcpy(result.pixels.data() + static_cast<size_t>(y) * result.rowPitch,
                    src + static_cast<size_t>(y) * mapped.RowPitch,
                    result.rowPitch);
    }

    context->Unmap(staging.get(), 0);
    session.Close();
    framePool.Close();
    return true;
}

}

bool CaptureWindow(HWND hwnd, uint32_t timeoutMs, CaptureResult& result, std::wstring& error) {
    try {
        if (!GraphicsCaptureSession::IsSupported()) {
            error = L"Windows.Graphics.Capture is not supported on this device.";
            return false;
        }
        GraphicsCaptureItem item = CreateItemForWindow(hwnd);
        if (item == nullptr) {
            error = L"Failed to create a capture item for the window.";
            return false;
        }
        return GrabItem(item, timeoutMs, result, error);
    } catch (const winrt::hresult_error& ex) {
        error = std::wstring(L"Capture failed: ") + ex.message().c_str();
        return false;
    }
}

bool CaptureMonitor(HMONITOR monitor, uint32_t timeoutMs, CaptureResult& result, std::wstring& error) {
    try {
        if (!GraphicsCaptureSession::IsSupported()) {
            error = L"Windows.Graphics.Capture is not supported on this device.";
            return false;
        }
        GraphicsCaptureItem item = CreateItemForMonitor(monitor);
        if (item == nullptr) {
            error = L"Failed to create a capture item for the display.";
            return false;
        }
        return GrabItem(item, timeoutMs, result, error);
    } catch (const winrt::hresult_error& ex) {
        error = std::wstring(L"Capture failed: ") + ex.message().c_str();
        return false;
    }
}

}
