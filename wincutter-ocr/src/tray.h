#pragma once

#include "common.h"

namespace wc {

bool TrayAdd(HWND hwnd, HINSTANCE hInst, UINT callbackMsg, const std::wstring& tip);
void TrayRemove(HWND hwnd);
void TrayBalloon(HWND hwnd, const std::wstring& title, const std::wstring& text);

} // namespace wc
