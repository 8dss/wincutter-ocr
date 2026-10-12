#include "overlay.h"

#include <windowsx.h>

#include <algorithm>

namespace wc {

namespace {

const wchar_t* kOverlayClass = L"WinCutterOverlay";
const int kAlpha = 100;
const COLORREF kTransparentKey = RGB(1, 2, 3);

bool  g_done = false;
bool  g_dragging = false;
bool  g_hasRect = false;
POINT g_start{};
POINT g_cur{};
RECT  g_virtual{};
RECT  g_lastClient{};

HBRUSH g_darkBrush = nullptr;
HBRUSH g_holeBrush = nullptr;
HPEN   g_borderPen = nullptr;

RECT NormalizedSelection() {
    RECT r{};
    r.left   = std::min(g_start.x, g_cur.x);
    r.top    = std::min(g_start.y, g_cur.y);
    r.right  = std::max(g_start.x, g_cur.x);
    r.bottom = std::max(g_start.y, g_cur.y);
    return r;
}

RECT ClientRectOfSelection() {
    RECT r = NormalizedSelection();
    r.left   -= g_virtual.left;
    r.top    -= g_virtual.top;
    r.right  -= g_virtual.left;
    r.bottom -= g_virtual.top;
    return r;
}

void InvalidateChangedArea(HWND hwnd) {
    RECT now{};
    if (g_dragging) {
        now = ClientRectOfSelection();
    }
    RECT u{};
    UnionRect(&u, &g_lastClient, &now);
    InflateRect(&u, 6, 6);
    InvalidateRect(hwnd, &u, FALSE);
    g_lastClient = now;
}

LRESULT CALLBACK OverlayProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_LBUTTONDOWN:
        g_dragging = true;
        g_start.x = GET_X_LPARAM(lp) + g_virtual.left;
        g_start.y = GET_Y_LPARAM(lp) + g_virtual.top;
        g_cur = g_start;
        SetCapture(hwnd);
        InvalidateChangedArea(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        if (g_dragging) {
            g_cur.x = GET_X_LPARAM(lp) + g_virtual.left;
            g_cur.y = GET_Y_LPARAM(lp) + g_virtual.top;
            InvalidateChangedArea(hwnd);
        }
        return 0;

    case WM_LBUTTONUP:
        if (g_dragging) {
            g_dragging = false;
            g_cur.x = GET_X_LPARAM(lp) + g_virtual.left;
            g_cur.y = GET_Y_LPARAM(lp) + g_virtual.top;
            ReleaseCapture();
            RECT r = NormalizedSelection();
            g_hasRect = (r.right - r.left) > 1 && (r.bottom - r.top) > 1;
            g_done = true;
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;

    case WM_RBUTTONDOWN:
        g_done = true;
        g_hasRect = false;
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            g_done = true;
            g_hasRect = false;
            ShowWindow(hwnd, SW_HIDE);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rc;
        GetClientRect(hwnd, &rc);

        FillRect(hdc, &rc, g_darkBrush);

        if (g_dragging) {
            RECT sel = ClientRectOfSelection();
            FillRect(hdc, &sel, g_holeBrush);

            HGDIOBJ oldPen = SelectObject(hdc, g_borderPen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, sel.left, sel.top, sel.right, sel.bottom);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
        }

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProc(hwnd, msg, wp, lp);
}

} // namespace

std::optional<RECT> RunSelectionOverlay(HINSTANCE hInst) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEX wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = OverlayProc;
        wc.hInstance = hInst;
        wc.lpszClassName = kOverlayClass;
        wc.hCursor = LoadCursor(nullptr, IDC_CROSS);
        wc.hbrBackground = nullptr;
        if (!RegisterClassExW(&wc)) {
            Log(L"[overlay] RegisterClassEx failed: " + std::to_wstring(GetLastError()));
            return std::nullopt;
        }
        registered = true;
    }

    if (!g_darkBrush) g_darkBrush = CreateSolidBrush(RGB(0, 0, 0));
    if (!g_holeBrush) g_holeBrush = CreateSolidBrush(kTransparentKey);
    if (!g_borderPen) g_borderPen = CreatePen(PS_SOLID, 2, RGB(0, 160, 255));

    g_virtual.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    g_virtual.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    g_done = false;
    g_dragging = false;
    g_hasRect = false;
    g_lastClient = {};

    HWND hwnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        kOverlayClass, L"", WS_POPUP,
        g_virtual.left, g_virtual.top, vw, vh,
        nullptr, nullptr, hInst, nullptr);

    if (!hwnd) {
        Log(L"[overlay] CreateWindowEx failed: " + std::to_wstring(GetLastError()));
        return std::nullopt;
    }

    SetLayeredWindowAttributes(hwnd, kTransparentKey, kAlpha, LWA_COLORKEY | LWA_ALPHA);
    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    MSG msg;
    while (!g_done) {
        if (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            break;
        }
    }

    DestroyWindow(hwnd);

    if (!g_hasRect) {
        return std::nullopt;
    }
    return NormalizedSelection();
}

} // namespace wc
