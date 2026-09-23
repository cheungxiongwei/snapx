#include "args.h"

#include <cwchar>
#include <cwctype>
#include <cmath>
#include <algorithm>

namespace snapx {

namespace {

constexpr double kScaleMin = 0.0;
constexpr double kScaleMax = 5.0;

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

std::wstring ToLower(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t c) {
        return static_cast<wchar_t>(std::towlower(c));
    });
    return value;
}

std::wstring StripQuotes(const std::wstring& value) {
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        return value.substr(1, value.size() - 2);
    }
    return value;
}

bool ParseUint32(const std::wstring& text, uint32_t& out) {
    if (text.empty()) {
        return false;
    }
    wchar_t* end = nullptr;
    unsigned long value = std::wcstoul(text.c_str(), &end, 10);
    if (end == text.c_str() || *end != L'\0') {
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

bool ParseDouble(const std::wstring& text, double& out) {
    if (text.empty()) {
        return false;
    }
    wchar_t* end = nullptr;
    double value = std::wcstod(text.c_str(), &end);
    if (end == text.c_str() || *end != L'\0') {
        return false;
    }
    out = value;
    return true;
}

bool ValueOf(int argc, wchar_t** argv, int& index, const std::wstring& inlineValue,
             bool hasInline, std::wstring& out) {
    if (hasInline) {
        out = inlineValue;
        return true;
    }
    if (index + 1 >= argc) {
        return false;
    }
    ++index;
    out = argv[index];
    return true;
}

}

std::wstring FormatExtension(ImageFormat format) {
    switch (format) {
    case ImageFormat::Png:
        return L".png";
    case ImageFormat::Jpeg:
        return L".jpg";
    case ImageFormat::Bmp:
        return L".bmp";
    }
    return L".png";
}

bool ParseFormat(const std::wstring& text, ImageFormat& format) {
    std::wstring lower = ToLower(text);
    if (lower == L"png") {
        format = ImageFormat::Png;
        return true;
    }
    if (lower == L"jpg" || lower == L"jpeg") {
        format = ImageFormat::Jpeg;
        return true;
    }
    if (lower == L"bmp") {
        format = ImageFormat::Bmp;
        return true;
    }
    return false;
}

bool ResolveFormatFromOutput(const std::wstring& output, ImageFormat& format) {
    const std::wstring lower = ToLower(output);
    const size_t dot = lower.find_last_of(L'.');
    const size_t slash = lower.find_last_of(L"\\/");
    const bool hasExtension = dot != std::wstring::npos &&
                              (slash == std::wstring::npos || dot > slash);
    if (!hasExtension) {
        format = ImageFormat::Png;
        return true;
    }
    return ParseFormat(lower.substr(dot + 1), format);
}

bool ParseCommandLine(int argc, wchar_t** argv, Options& options, std::wstring& error) {
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        std::wstring inlineValue;
        bool hasInline = false;
        const size_t eq = arg.find(L'=');
        if (arg.rfind(L"--", 0) == 0 && eq != std::wstring::npos) {
            inlineValue = arg.substr(eq + 1);
            arg = arg.substr(0, eq);
            hasInline = true;
        }

        const std::wstring lowered = ToLower(arg);
        std::wstring value;

        if (!hasInline && lowered == L"scan") {
            if (options.command != Command::None) {
                error = L"unknown option '" + arg + L"'";
                return false;
            }
            options.command = Command::Scan;
        } else if (!hasInline && lowered == L"capture") {
            if (options.command != Command::None) {
                error = L"unknown option '" + arg + L"'";
                return false;
            }
            options.command = Command::Capture;
        } else if (lowered == L"--help") {
            if (options.command == Command::Scan) {
                options.command = Command::ScanHelp;
            } else if (options.command == Command::Capture) {
                options.command = Command::CaptureHelp;
            } else {
                options.command = Command::Help;
            }
        } else if (lowered == L"--pid") {
            if (options.command != Command::Capture) {
                error = L"unknown option '" + arg + L"'";
                return false;
            }
            if (!ValueOf(argc, argv, i, inlineValue, hasInline, value)) {
                error = L"--pid requires a value";
                return false;
            }
            uint32_t pid = 0;
            if (!ParseUint32(value, pid)) {
                error = L"invalid pid '" + value + L"'";
                return false;
            }
            options.hasPid = true;
            options.pid = pid;
        } else if (lowered == L"--output" || lowered == L"-o") {
            if (options.command != Command::Capture) {
                error = L"unknown option '" + arg + L"'";
                return false;
            }
            if (!ValueOf(argc, argv, i, inlineValue, hasInline, value)) {
                error = L"--output requires a value";
                return false;
            }
            value = StripQuotes(value);
            if (value == L"-") {
                options.outputToStdout = true;
            } else {
                options.output = value;
            }
        } else if (lowered == L"--scale" || lowered == L"-s") {
            if (options.command != Command::Capture) {
                error = L"unknown option '" + arg + L"'";
                return false;
            }
            if (!ValueOf(argc, argv, i, inlineValue, hasInline, value)) {
                error = L"--scale requires a value";
                return false;
            }
            double scale = 0.0;
            if (!ParseDouble(value, scale)) {
                error = L"invalid scale value '" + value + L"'";
                return false;
            }
            if (!(scale > kScaleMin) || scale > kScaleMax) {
                error = L"--scale must be in (0.0, 5.0], got " + value;
                return false;
            }
            options.scale = scale;
        } else if (lowered == L"--quality" || lowered == L"-q") {
            if (options.command != Command::Capture) {
                error = L"unknown option '" + arg + L"'";
                return false;
            }
            if (!ValueOf(argc, argv, i, inlineValue, hasInline, value)) {
                error = L"--quality requires a value";
                return false;
            }
            uint32_t quality = 0;
            if (!ParseUint32(value, quality) || quality < 1 || quality > 100) {
                error = L"--quality must be 1-100, got " + value;
                return false;
            }
            options.jpegQuality = static_cast<int>(quality);
        } else {
            error = L"unknown option '" + arg + L"'";
            return false;
        }
    }

    if (options.command == Command::Help || options.command == Command::ScanHelp ||
        options.command == Command::CaptureHelp) {
        return true;
    }

    if (options.command == Command::Scan) {
        return true;
    }

    if (options.command != Command::Capture) {
        error = L"no command given; use scan or capture";
        return false;
    }

    if (!options.hasPid) {
        error = L"--pid is required";
        return false;
    }

    if (!options.outputToStdout) {
        if (!options.output.empty()) {
            ImageFormat format = ImageFormat::Png;
            if (!ResolveFormatFromOutput(options.output, format)) {
                const size_t dot = options.output.find_last_of(L'.');
                const std::wstring ext = (dot == std::wstring::npos)
                                             ? options.output
                                             : options.output.substr(dot);
                error = L"unsupported output extension '" + ext +
                        L"'; use png, jpeg, or bmp";
                return false;
            }
        }
    }

    return true;
}

void PrintUsage() {
    std::wstring text;
    text += L"usage:\n";
    text += L"  snapx scan [--help]\n";
    text += L"  snapx capture --pid <id> [-o <path>] [-s <n>] [-q <n>]\n";
    WriteStream(GetStdHandle(STD_ERROR_HANDLE), text);
}

void PrintHelp() {
    std::wstring text;
    text += L"snapx - capture a Windows application window to an image file\n";
    text += L"\n";
    text += L"usage:\n";
    text += L"  snapx scan [--help]\n";
    text += L"  snapx capture --pid <id> [-o <path>] [-s <n>] [-q <n>]\n";
    text += L"\n";
    text += L"commands:\n";
    text += L"  scan      list capturable windows\n";
    text += L"  capture   capture one window by process id\n";
    text += L"\n";
    text += L"run 'snapx <command> --help' for details\n";
    WriteStream(GetStdHandle(STD_OUTPUT_HANDLE), text);
}

void PrintScanHelp() {
    std::wstring text;
    text += L"snapx scan - list capturable windows\n";
    text += L"\n";
    text += L"usage:\n";
    text += L"  snapx scan\n";
    text += L"\n";
    text += L"examples:\n";
    text += L"  snapx scan\n";
    text += L"    PID      PROCESS NAME        TITLE\n";
    text += L"    4608     Code.exe            main.cpp - Visual Studio Code\n";
    text += L"    21604    WindowsTerminal.exe Windows PowerShell\n";
    WriteStream(GetStdHandle(STD_OUTPUT_HANDLE), text);
}

void PrintCaptureHelp() {
    std::wstring text;
    text += L"snapx capture - capture one window by process id\n";
    text += L"\n";
    text += L"usage:\n";
    text += L"  snapx capture --pid <id> [-o <path>] [-s <n>] [-q <n>]\n";
    text += L"\n";
    text += L"options:\n";
    text += L"  --pid <id>        process id to capture (required)\n";
    text += L"  -o, --output <p>  output path, '-' for stdout, else <process>_<pid>_<time>.<ext> (.png .jpg .jpeg .bmp)\n";
    text += L"  -s, --scale <n>   output size multiplier, default 1.0, range (0.0, 5.0]\n";
    text += L"  -q, --quality <n> JPEG quality, default 90, range 1-100\n";
    text += L"\n";
    text += L"examples:\n";
    text += L"  snapx capture --pid 4608\n";
    text += L"  snapx capture --pid 4608 -o shot.png\n";
    text += L"  snapx capture --pid 4608 -o shot.jpg -q 80\n";
    text += L"  snapx capture --pid 4608 -o - > shot.png\n";
    WriteStream(GetStdHandle(STD_OUTPUT_HANDLE), text);
}

}
