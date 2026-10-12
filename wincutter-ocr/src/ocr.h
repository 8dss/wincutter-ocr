#pragma once

#include "common.h"

#include <cstdint>
#include <vector>

namespace wc {

std::wstring OcrBgra(const uint8_t* pixels, int width, int height, const wchar_t* langTag);

std::wstring OcrBgraAuto(const uint8_t* pixels, int width, int height);

std::vector<std::wstring> OcrAvailableLanguages();

} // namespace wc
