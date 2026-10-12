#pragma once

#include "common.h"

namespace wc {

void ShowResultWindow(HINSTANCE hInst,
                      const std::wstring& source,
                      const std::wstring& translation,
                      POINT anchor);

} // namespace wc
