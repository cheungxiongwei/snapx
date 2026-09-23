#pragma once

#include <windows.h>
#include <string>
#include <cstdint>

namespace snapx {

enum class ImageFormat {
    Png,
    Jpeg,
    Bmp,
};

enum class Command {
    None,
    Help,
    ScanHelp,
    Scan,
    CaptureHelp,
    Capture,
};

struct Options {
    Command command = Command::None;

    bool hasPid = false;
    uint32_t pid = 0;

    std::wstring output;
    bool outputToStdout = false;

    double scale = 1.0;
    int jpegQuality = 90;
};

bool ParseCommandLine(int argc, wchar_t** argv, Options& options, std::wstring& error);

void PrintUsage();
void PrintHelp();
void PrintScanHelp();
void PrintCaptureHelp();

std::wstring FormatExtension(ImageFormat format);
bool ParseFormat(const std::wstring& text, ImageFormat& format);
bool ResolveFormatFromOutput(const std::wstring& output, ImageFormat& format);

}
