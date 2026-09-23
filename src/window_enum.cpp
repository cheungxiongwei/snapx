#include "window_enum.h"

#include <psapi.h>
#include <algorithm>

namespace snapx {

namespace {

struct EnumContext {
    std::vector<WindowInfo>* windows;
};

BOOL CALLBACK EnumProc(HWND hwnd, LPARAM param) {
    auto* context = reinterpret_cast<EnumContext*>(param);
    if (GetWindowTextLengthW(hwnd) == 0) {
        return TRUE;
    }
    if (GetWindow(hwnd, GW_OWNER) != nullptr) {
        return TRUE;
    }

    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if ((exStyle & WS_EX_TOOLWINDOW) != 0) {
        return TRUE;
    }

    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    const bool visible = (style & WS_VISIBLE) != 0;
    const bool minimized = IsIconic(hwnd) != FALSE;
    if (!visible && !minimized) {
        return TRUE;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == 0) {
        return TRUE;
    }

    WindowInfo info;
    info.hwnd = hwnd;
    info.processId = pid;
    info.minimized = minimized;
    info.processName = ProcessNameForPid(pid);

    std::wstring title;
    for (int capacity = 256; capacity <= 65536; capacity *= 2) {
        title.resize(static_cast<size_t>(capacity));
        const int copied = GetWindowTextW(hwnd, title.data(), capacity);
        if (copied < capacity - 1) {
            title.resize(static_cast<size_t>(copied));
            break;
        }
    }
    info.title = std::move(title);

    context->windows->push_back(std::move(info));
    return TRUE;
}

}

std::wstring ProcessNameForPid(DWORD processId) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (process == nullptr) {
        return L"";
    }

    wchar_t buffer[MAX_PATH] = {};
    DWORD size = static_cast<DWORD>(std::size(buffer));
    std::wstring name;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
        std::wstring full(buffer, size);
        const size_t slash = full.find_last_of(L"\\/");
        name = (slash == std::wstring::npos) ? full : full.substr(slash + 1);
    }
    CloseHandle(process);
    return name;
}

std::vector<WindowInfo> EnumerateWindows() {
    std::vector<WindowInfo> windows;
    EnumContext context{ &windows };
    EnumWindows(EnumProc, reinterpret_cast<LPARAM>(&context));
    return windows;
}

bool FindWindowByPid(uint32_t pid, WindowInfo& window) {
    const auto windows = EnumerateWindows();

    bool haveMinimized = false;
    WindowInfo minimizedFallback;
    for (const WindowInfo& candidate : windows) {
        if (candidate.processId != pid) {
            continue;
        }
        if (!candidate.minimized) {
            window = candidate;
            return true;
        }
        if (!haveMinimized) {
            minimizedFallback = candidate;
            haveMinimized = true;
        }
    }

    if (haveMinimized) {
        window = minimizedFallback;
        return true;
    }
    return false;
}

}
