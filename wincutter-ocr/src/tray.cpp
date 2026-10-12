#include "tray.h"

#include <shellapi.h>

namespace wc {

namespace {
NOTIFYICONDATAW g_nid{};
const UINT kTrayId = 1;
}

bool TrayAdd(HWND hwnd, HINSTANCE hInst, UINT callbackMsg, const std::wstring& tip) {
    (void)hInst;
    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = kTrayId;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = callbackMsg;
    g_nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(g_nid.szTip, tip.c_str(), _TRUNCATE);
    return Shell_NotifyIconW(NIM_ADD, &g_nid) == TRUE;
}

void TrayRemove(HWND hwnd) {
    g_nid.hWnd = hwnd;
    g_nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

void TrayBalloon(HWND hwnd, const std::wstring& title, const std::wstring& text) {
    g_nid.hWnd = hwnd;
    g_nid.uID = kTrayId;
    g_nid.uFlags = NIF_INFO;
    g_nid.dwInfoFlags = NIIF_INFO;
    wcsncpy_s(g_nid.szInfoTitle, title.c_str(), _TRUNCATE);
    wcsncpy_s(g_nid.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

} // namespace wc
