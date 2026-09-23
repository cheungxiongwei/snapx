#pragma once

#include <windows.h>
#include <string>
#include <vector>
#include <cstdint>

namespace snapx {

struct WindowInfo {
    HWND hwnd = nullptr;
    DWORD processId = 0;
    std::wstring processName;
    std::wstring title;
    bool minimized = false;
};

std::vector<WindowInfo> EnumerateWindows();

bool FindWindowByPid(uint32_t pid, WindowInfo& window);

std::wstring ProcessNameForPid(DWORD processId);

}
