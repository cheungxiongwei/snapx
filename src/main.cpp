#include "args.h"
#include "capture.h"
#include "clipboard.h"
#include "encode.h"
#include "window_enum.h"

#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdio>
#include <ctime>
#include <cmath>

namespace {

using snapx::ImageFormat;

void WriteStream(HANDLE handle, const std::wstring& text) {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD mode = 0;
    if (GetConsoleMode(handle, &mode)) {
        DWORD written = 0;
        WriteConsoleW(handle, text.c_str(), static_cast<DWORD>(text.size()), &written, nullptr);
        return;
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return;
    }
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                        utf8.data(), size, nullptr, nullptr);
    DWORD written = 0;
    WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

void WriteStdOut(const std::wstring& text) {
    WriteStream(GetStdHandle(STD_OUTPUT_HANDLE), text);
}

void WriteStdErr(const std::wstring& text) {
    WriteStream(GetStdHandle(STD_ERROR_HANDLE), text);
}

[[noreturn]] void FailUsage(const std::wstring& message) {
    WriteStdErr(L"error: " + message + L"\n");
    snapx::PrintUsage();
    ExitProcess(2);
}

std::wstring SanitizeFileName(const std::wstring& input) {
    std::wstring result = input;
    const std::wstring invalid = L"<>:\"/\\|?*";
    for (wchar_t& c : result) {
        if (invalid.find(c) != std::wstring::npos || c < 32) {
            c = L'_';
        }
    }
    if (result.empty()) {
        result = L"window";
    }
    return result;
}

std::wstring TimestampNow() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t buffer[32] = {};
    wcsftime(buffer, std::size(buffer), L"%Y%m%d-%H%M%S", &local);
    return buffer;
}

std::wstring AutoOutputName(const snapx::WindowInfo& window, ImageFormat format) {
    std::wstring base = window.processName.empty() ? L"window" : window.processName;
    const size_t dot = base.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        base = base.substr(0, dot);
    }
    return SanitizeFileName(base) + L"_" + std::to_wstring(window.processId) + L"_" +
           TimestampNow() + snapx::FormatExtension(format);
}

std::wstring AutoScreenName(ImageFormat format) {
    return L"screen_" + TimestampNow() + snapx::FormatExtension(format);
}

int RunList() {
    const auto windows = snapx::EnumerateWindows();
    std::wstring text;
    text += L"PID      PROCESS NAME                  TITLE\n";
    text += L"-------  ----------------------------  -----\n";
    for (const auto& window : windows) {
        wchar_t line[1024] = {};
        swprintf_s(line, L"%-7lu  %-28.28s  %s%s\n",
                   window.processId,
                   window.processName.c_str(),
                   window.title.c_str(),
                   window.minimized ? L" [minimized]" : L"");
        text += line;
    }
    WriteStdOut(text);
    return 0;
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

void WriteToStdout(const snapx::CaptureResult& image, ImageFormat format, int quality) {
    wchar_t tempDir[MAX_PATH] = {};
    GetTempPathW(ARRAYSIZE(tempDir), tempDir);
    wchar_t tempFile[MAX_PATH] = {};
    GetTempFileNameW(tempDir, L"snx", 0, tempFile);
    std::wstring tempPath = tempFile;

    std::wstring error;
    if (!snapx::EncodeToFile(image, tempPath, format, quality, 1.0, error)) {
        DeleteFileW(tempPath.c_str());
        WriteStdErr(L"error: " + error + L"\n");
        ExitProcess(1);
    }

    HANDLE file = CreateFileW(tempPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        DeleteFileW(tempPath.c_str());
        WriteStdErr(L"error: cannot read temporary encode result\n");
        ExitProcess(1);
    }

    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    std::vector<char> buffer(65536);
    DWORD read = 0;
    while (ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) &&
           read > 0) {
        DWORD written = 0;
        if (!WriteFile(out, buffer.data(), read, &written, nullptr) || written != read) {
            break;
        }
    }
    CloseHandle(file);
    DeleteFileW(tempPath.c_str());
}

}

int wmain(int argc, wchar_t** argv) {
    snapx::Options options;
    std::wstring error;
    if (!snapx::ParseCommandLine(argc, argv, options, error)) {
        FailUsage(error);
    }

    switch (options.command) {
    case snapx::Command::Help:
        snapx::PrintHelp();
        return 0;
    case snapx::Command::ScanHelp:
        snapx::PrintScanHelp();
        return 0;
    case snapx::Command::CaptureHelp:
        snapx::PrintCaptureHelp();
        return 0;
    case snapx::Command::ScreenHelp:
        snapx::PrintScreenHelp();
        return 0;
    case snapx::Command::Scan:
        return RunList();
    case snapx::Command::Capture:
    case snapx::Command::Screen:
        break;
    case snapx::Command::None:
        FailUsage(L"no command given; use scan, capture, or screen");
    }

    const bool fullScreen = options.command == snapx::Command::Screen;
    snapx::WindowInfo window;
    snapx::CaptureResult image;
    bool captured = false;

    if (fullScreen) {
        HMONITOR monitor = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
        captured = snapx::CaptureMonitor(monitor, 5000, image, error);
    } else {
        if (!snapx::FindWindowByPid(options.pid, window)) {
            WriteStdErr(L"error: no capturable window for pid " +
                        std::to_wstring(options.pid) + L"\n");
            return 1;
        }

        const bool wasMinimized = window.minimized;
        if (wasMinimized) {
            ShowWindow(window.hwnd, SW_RESTORE);
            const DWORD deadline = GetTickCount() + 2000;
            while (IsIconic(window.hwnd) && GetTickCount() < deadline) {
                Sleep(20);
            }
            Sleep(100);
        }

        captured = snapx::CaptureWindow(window.hwnd, 5000, image, error);

        if (wasMinimized) {
            ShowWindow(window.hwnd, SW_MINIMIZE);
        }
    }

    if (!captured) {
        WriteStdErr(L"error: " + error + L"\n");
        return 1;
    }

    const uint32_t outWidth = static_cast<uint32_t>(
        std::lround(static_cast<double>(image.width) * options.scale));
    const uint32_t outHeight = static_cast<uint32_t>(
        std::lround(static_cast<double>(image.height) * options.scale));

    if (options.clipboard) {
        if (!snapx::CopyImageToClipboard(image, options.scale, error)) {
            WriteStdErr(L"error: " + error + L"\n");
            return 1;
        }
        WriteStdOut(L"copied " + std::to_wstring(outWidth) + L"x" +
                    std::to_wstring(outHeight) + L" to clipboard\n");
        return 0;
    }

    ImageFormat format = ImageFormat::Png;
    if (!options.outputToStdout && !options.output.empty()) {
        snapx::ResolveFormatFromOutput(options.output, format);
    }

    if (options.outputToStdout) {
        WriteToStdout(image, format, options.jpegQuality);
        return 0;
    }

    std::wstring path = options.output.empty()
                            ? (fullScreen ? AutoScreenName(format)
                                          : AutoOutputName(window, format))
                            : options.output;
    if (!snapx::EncodeToFile(image, path, format, options.jpegQuality, options.scale, error)) {
        WriteStdErr(L"error: " + error + L"\n");
        return 1;
    }

    WriteStdOut(L"saved " + std::to_wstring(outWidth) + L"x" +
                std::to_wstring(outHeight) + L" (" + FormatName(format) + L") to " +
                path + L"\n");
    return 0;
}
