#pragma once

#include <windows.h>
#include <d3d11.h>
#include <cstdint>
#include <string>

namespace snapx {

struct CaptureResult {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t rowPitch = 0;
    std::string pixels;
};

bool CaptureWindow(HWND hwnd, uint32_t timeoutMs, CaptureResult& result, std::wstring& error);

bool CaptureMonitor(HMONITOR monitor, uint32_t timeoutMs, CaptureResult& result, std::wstring& error);

}
