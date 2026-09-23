#pragma once

#include "args.h"
#include "capture.h"

#include <string>

namespace snapx {

bool EncodeToFile(const CaptureResult& image, const std::wstring& path,
                  ImageFormat format, int jpegQuality, double scale, std::wstring& error);

}
