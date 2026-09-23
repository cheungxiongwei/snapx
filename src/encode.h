#pragma once

#include "args.h"
#include "capture.h"

#include <string>
#include <vector>
#include <cstdint>

namespace snapx {

bool EncodeToFile(const CaptureResult& image, const std::wstring& path,
                  ImageFormat format, int jpegQuality, double scale, std::wstring& error);

bool EncodeToMemory(const CaptureResult& image, ImageFormat format, int jpegQuality,
                    double scale, std::vector<uint8_t>& bytes, std::wstring& error);

bool ScaleToBgra(const CaptureResult& image, double scale, uint32_t& width, uint32_t& height,
                 std::vector<uint8_t>& pixels, std::wstring& error);

}
