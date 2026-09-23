#pragma once

#include "capture.h"

#include <string>

namespace snapx {

bool CopyImageToClipboard(const CaptureResult& image, double scale, std::wstring& error);

}
