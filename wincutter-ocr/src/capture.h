#pragma once

#include "common.h"

#include <cstdint>
#include <vector>

namespace wc {

struct ImageBgra {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

bool CaptureScreenRect(const RECT& rect, ImageBgra& out);

} // namespace wc
