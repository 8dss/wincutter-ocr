#include "result_window.h"
#include "clipboard.h"
#include "translate.h"

#include <algorithm>
#include <cwctype>
#include <windowsx.h>

namespace wc {

namespace {

const wchar_t* kResultClass = L"WinCutterResult";
const int kEscHotkeyId = 9200;

const int ID_BTN_EN = 1;
const int ID_BTN_RU = 2;
const int ID_BTN_ALL = 3;

const wchar_t* kBtnLabels[3] = { L"EN", L"RU", L"EN - RU" };

const DWORD kWinStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
const DWORD kWinExStyle = WS_EX_TOPMOST | WS_EX_TOOLWINDOW;

std::wstring g_source;
std::wstring g_translation;
bool g_done = false;
HINSTANCE g_hInst = nullptr;
UINT g_dpi = 96;

HFONT g_font = nullptr;
HFONT g_fontBold = nullptr;
HBRUSH g_bg = nullptr;
HBRUSH g_sep = nullptr;

int g_pad = 14;
int g_gap = 10;
int g_btnGap = 8;
int g_btnH = 30;
int g_width = 460;
int g_maxH = 520;
int g_minTextH = 40;
int g_contentW = 432;

const wchar_t* kTipClass = L"WinCutterWordTip";
const UINT_PTR kTipTimerId = 77;

HWND g_tip = nullptr;
std::wstring g_tipText;
HFONT g_tipFont = nullptr;
HBRUSH g_tipBg = nullptr;
HPEN g_tipBorder = nullptr;
int g_tipPadX = 8;
int g_tipPadY = 5;
WNDPROC g_srcEditProc = nullptr;
WNDPROC g_transEditProc = nullptr;

int g_srcH = 0;
int g_transH = 0;
int g_sepY = 0;
int g_transY = 0;
int g_btnY = 0;
bool g_srcScroll = false;
bool g_transScroll = false;

HWND g_srcEdit = nullptr;
HWND g_transEdit = nullptr;
HWND g_buttons[3] = { nullptr, nullptr, nullptr };

int S(int v) { return MulDiv(v, static_cast<int>(g_dpi), 96); }

void SetMetrics(UINT dpi) {
    g_dpi = dpi;
    g_pad = S(14);
    g_gap = S(10);
    g_btnGap = S(8);
    g_btnH = S(30);
    g_width = S(460);
    g_maxH = S(520);
    g_minTextH = S(40);
    g_contentW = g_width - 2 * g_pad;

    if (g_font) DeleteObject(g_font);
    if (g_fontBold) DeleteObject(g_fontBold);
    g_font = CreateFontW(-S(18), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontBold = CreateFontW(-S(19), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    if (g_tipFont) DeleteObject(g_tipFont);
    g_tipFont = CreateFontW(-S(17), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    g_tipPadX = S(8);
    g_tipPadY = S(5);

    if (!g_bg) g_bg = CreateSolidBrush(RGB(252, 252, 252));
    if (!g_sep) g_sep = CreateSolidBrush(RGB(220, 220, 220));
    if (!g_tipBg) g_tipBg = CreateSolidBrush(RGB(255, 255, 225));
    if (!g_tipBorder) g_tipBorder = CreatePen(PS_SOLID, 1, RGB(170, 170, 130));
}

int MeasureHeight(HDC hdc, HFONT font, const std::wstring& text, int width) {
    HGDIOBJ old = SelectObject(hdc, font);
    RECT r{ 0, 0, width, 0 };
    DrawTextW(hdc, text.c_str(), -1, &r,
        DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, old);
    return (r.bottom - r.top) + S(8);
}

void ComputeLayout(HDC hdc) {
    int sbW = GetSystemMetricsForDpi(SM_CXVSCROLL, g_dpi);
    int measureW = g_contentW - sbW - S(4);

    int srcNeeded = MeasureHeight(hdc, g_font, g_source, measureW);
    int transNeeded = MeasureHeight(hdc, g_fontBold, g_translation, measureW);
    srcNeeded = std::max(srcNeeded, g_minTextH);
    transNeeded = std::max(transNeeded, g_minTextH);

    int fixedPart = 2 * g_pad + 3 * g_gap + 1 + g_btnH;
    int avail = std::max(g_maxH - fixedPart, 2 * g_minTextH);

    int srcH = srcNeeded;
    int transH = transNeeded;
    if (srcH + transH > avail) {
        int half = avail / 2;
        if (srcH <= half) {
            transH = avail - srcH;
        } else if (transH <= half) {
            srcH = avail - transH;
        } else {
            srcH = half;
            transH = avail - half;
        }
    }

    g_srcH = srcH;
    g_transH = transH;
    g_srcScroll = srcNeeded > srcH;
    g_transScroll = transNeeded > transH;

    g_sepY = g_pad + g_srcH + g_gap;
    g_transY = g_sepY + 1 + g_gap;
    g_btnY = g_transY + g_transH + g_gap;
}

int ClientHeight() {
    return g_btnY + g_btnH + g_pad;
}

bool IsWordChar(wchar_t c) {
    return std::iswalnum(c) != 0 || c == L'\'' || c == L'-' || c == 0x2019;
}

void SizeTipToText(int& w, int& h) {
    HDC hdc = GetDC(g_tip);
    HGDIOBJ old = SelectObject(hdc, g_tipFont);
    RECT r{ 0, 0, S(260), 0 };
    DrawTextW(hdc, g_tipText.c_str(), -1, &r,
        DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(hdc, old);
    ReleaseDC(g_tip, hdc);
    w = (r.right - r.left) + 2 * g_tipPadX;
    h = (r.bottom - r.top) + 2 * g_tipPadY;
}

void HideTip(HWND owner) {
    if (g_tip) ShowWindow(g_tip, SW_HIDE);
    if (owner) KillTimer(owner, kTipTimerId);
}

LRESULT CALLBACK TipProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        FillRect(hdc, &rc, g_tipBg);

        HGDIOBJ oldPen = SelectObject(hdc, g_tipBorder);
        HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, oldBr);
        SelectObject(hdc, oldPen);

        RECT tr = rc;
        InflateRect(&tr, -g_tipPadX, -g_tipPadY);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(40, 40, 40));
        HGDIOBJ oldF = SelectObject(hdc, g_tipFont);
        DrawTextW(hdc, g_tipText.c_str(), -1, &tr,
            DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(hdc, oldF);

        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void ShowTip(HWND owner, const std::wstring& text, int x, int y, int lineH) {
    if (text.empty()) {
        HideTip(owner);
        return;
    }
    g_tipText = text;

    static bool tipRegistered = false;
    if (!tipRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = TipProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kTipClass;
        RegisterClassExW(&wc);
        tipRegistered = true;
    }
    if (!g_tip) {
        g_tip = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kTipClass, L"", WS_POPUP, 0, 0, 10, 10,
            owner, nullptr, g_hInst, nullptr);
        if (!g_tip) return;
    }

    int w = 0, h = 0;
    SizeTipToText(w, h);

    int tx = x;
    int ty = y - h - S(4);
    HMONITOR mon = MonitorFromPoint(POINT{ tx, ty }, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        if (tx + w > mi.rcWork.right) tx = mi.rcWork.right - w;
        if (tx < mi.rcWork.left) tx = mi.rcWork.left;
        if (ty < mi.rcWork.top) ty = y + lineH + S(4);
    }

    SetWindowPos(g_tip, HWND_TOPMOST, tx, ty, w, h,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(g_tip, nullptr, TRUE);
    SetTimer(owner, kTipTimerId, 5000, nullptr);
}

void HandleWordClick(HWND owner, HWND edit, int charIndex, bool isSource) {
    if (charIndex < 0) {
        HideTip(owner);
        return;
    }
    int len = GetWindowTextLengthW(edit);
    if (len <= 0) {
        HideTip(owner);
        return;
    }

    std::wstring text(len + 1, L'\0');
    int got = GetWindowTextW(edit, text.data(), len + 1);
    text.resize(got > 0 ? got : 0);
    if (text.empty()) {
        HideTip(owner);
        return;
    }
    if (charIndex > got) charIndex = got;

    int b = charIndex;
    while (b > 0 && IsWordChar(text[b - 1])) --b;
    int e = charIndex;
    while (e < got && IsWordChar(text[e])) ++e;
    if (e <= b) {
        HideTip(owner);
        return;
    }

    std::wstring word = text.substr(b, e - b);
    std::wstring translated = Trim(Translate(word, L"auto", isSource ? L"ru" : L"en"));
    if (translated.empty() || translated == word) {
        HideTip(owner);
        return;
    }

    LRESULT p = SendMessageW(edit, EM_POSFROMCHAR, static_cast<WPARAM>(b), 0);
    int wx = static_cast<int>(static_cast<short>(LOWORD(static_cast<DWORD>(p))));
    int wy = static_cast<int>(static_cast<short>(HIWORD(static_cast<DWORD>(p))));
    if (wx < 0 || wy < 0) {
        HideTip(owner);
        return;
    }
    POINT sp{ wx, wy };
    ClientToScreen(edit, &sp);
    ShowTip(owner, translated, sp.x, sp.y, S(20));
}

LRESULT CALLBACK EditLinkProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool isSource) {
    WNDPROC orig = isSource ? g_srcEditProc : g_transEditProc;
    switch (msg) {
    case WM_LBUTTONDOWN: {
        POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        LRESULT ci = SendMessageW(hwnd, EM_CHARFROMPOS, 0, MAKELPARAM(pt.x, pt.y));
        HandleWordClick(GetParent(hwnd), hwnd,
            static_cast<int>(LOWORD(static_cast<DWORD>(ci))), isSource);
        break;
    }
    case WM_MOUSEWHEEL:
    case WM_VSCROLL:
    case WM_KEYDOWN:
        HideTip(GetParent(hwnd));
        break;
    }
    return CallWindowProcW(orig, hwnd, msg, wp, lp);
}

LRESULT CALLBACK SrcEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return EditLinkProc(hwnd, msg, wp, lp, true);
}

LRESULT CALLBACK TransEditProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    return EditLinkProc(hwnd, msg, wp, lp, false);
}

void CreateControls(HWND hwnd) {
    DWORD srcStyle = WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL;
    if (g_srcScroll) srcStyle |= WS_VSCROLL | ES_AUTOVSCROLL;
    g_srcEdit = CreateWindowExW(0, L"EDIT", L"",
        srcStyle, g_pad, g_pad, g_contentW, g_srcH,
        hwnd, nullptr, g_hInst, nullptr);
    SetWindowTextW(g_srcEdit, g_source.c_str());
    SendMessageW(g_srcEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_font), TRUE);
    g_srcEditProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_srcEdit, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(SrcEditProc)));

    DWORD transStyle = WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY | ES_NOHIDESEL;
    if (g_transScroll) transStyle |= WS_VSCROLL | ES_AUTOVSCROLL;
    g_transEdit = CreateWindowExW(0, L"EDIT", L"",
        transStyle, g_pad, g_transY, g_contentW, g_transH,
        hwnd, nullptr, g_hInst, nullptr);
    SetWindowTextW(g_transEdit, g_translation.c_str());
    SendMessageW(g_transEdit, WM_SETFONT, reinterpret_cast<WPARAM>(g_fontBold), TRUE);
    g_transEditProc = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrW(g_transEdit, GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(TransEditProc)));

    int bw = (g_width - 2 * g_pad - 2 * g_btnGap) / 3;
    int ids[3] = { ID_BTN_EN, ID_BTN_RU, ID_BTN_ALL };
    for (int i = 0; i < 3; ++i) {
        g_buttons[i] = CreateWindowExW(0, L"BUTTON", kBtnLabels[i],
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            g_pad + i * (bw + g_btnGap), g_btnY, bw, g_btnH,
            hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(ids[i])),
            g_hInst, nullptr);
        if (g_buttons[i] && g_font) {
            SendMessageW(g_buttons[i], WM_SETFONT,
                reinterpret_cast<WPARAM>(g_font), TRUE);
        }
    }
}

void RestoreButtonLabels() {
    for (int i = 0; i < 3; ++i) {
        if (g_buttons[i]) {
            SetWindowTextW(g_buttons[i], kBtnLabels[i]);
        }
    }
}

void DoCopy(HWND hwnd, int which) {
    std::wstring text;
    int idx = 2;
    if (which == ID_BTN_EN) {
        text = g_source;
        idx = 0;
    } else if (which == ID_BTN_RU) {
        text = g_translation;
        idx = 1;
    } else {
        text = g_source + L" - " + g_translation;
    }

    CopyToClipboard(text);

    if (g_buttons[idx]) {
        SetWindowTextW(g_buttons[idx], L"\u2713");
        SetTimer(hwnd, 1, 900, nullptr);
    }
}

LRESULT CALLBACK ResultProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BTN_EN:  DoCopy(hwnd, ID_BTN_EN);  return 0;
        case ID_BTN_RU:  DoCopy(hwnd, ID_BTN_RU);  return 0;
        case ID_BTN_ALL: DoCopy(hwnd, ID_BTN_ALL); return 0;
        }
        return 0;

    case WM_TIMER:
        if (wp == 1) {
            KillTimer(hwnd, 1);
            RestoreButtonLabels();
        } else if (wp == kTipTimerId) {
            HideTip(hwnd);
        }
        return 0;

    case WM_HOTKEY:
        if (wp == kEscHotkeyId) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        HWND ctl = reinterpret_cast<HWND>(lp);
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, RGB(252, 252, 252));
        SetTextColor(hdc, (ctl == g_srcEdit) ? RGB(110, 110, 110) : RGB(20, 20, 20));
        return reinterpret_cast<LRESULT>(g_bg);
    }

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        FillRect(hdc, &rc, g_bg);

        RECT sepRc{ g_pad, g_sepY, g_width - g_pad, g_sepY + 1 };
        FillRect(hdc, &sepRc, g_sep);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_DESTROY:
        g_done = true;
        if (g_tip) {
            DestroyWindow(g_tip);
            g_tip = nullptr;
        }
        g_srcEdit = nullptr;
        g_transEdit = nullptr;
        g_srcEditProc = nullptr;
        g_transEditProc = nullptr;
        for (auto& b : g_buttons) {
            b = nullptr;
        }
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void ShowResultWindow(HINSTANCE hInst,
                      const std::wstring& source,
                      const std::wstring& translation,
                      POINT anchor) {
    g_source = source;
    g_translation = translation;
    g_done = false;
    g_hInst = hInst;

    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = ResultProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = kResultClass;
        wc.hbrBackground = nullptr;
        if (!RegisterClassExW(&wc)) {
            return;
        }
        registered = true;
    }

    HWND hwnd = CreateWindowExW(
        kWinExStyle, kResultClass, L"WinCutter", kWinStyle,
        anchor.x, anchor.y, 480, 320, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        return;
    }

    SetMetrics(GetDpiForWindow(hwnd));

    HDC hdc = GetDC(hwnd);
    ComputeLayout(hdc);
    ReleaseDC(hwnd, hdc);

    int clientH = ClientHeight();
    RECT wr{ 0, 0, g_width, clientH };
    AdjustWindowRectExForDpi(&wr, kWinStyle, FALSE, kWinExStyle, g_dpi);
    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;

    POINT pos{ anchor.x + S(18), anchor.y + S(18) };
    HMONITOR mon = MonitorFromPoint(pos, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(mon, &mi)) {
        if (pos.x + winW > mi.rcWork.right)  pos.x = mi.rcWork.right - winW;
        if (pos.y + winH > mi.rcWork.bottom) pos.y = mi.rcWork.bottom - winH;
        if (pos.x < mi.rcWork.left) pos.x = mi.rcWork.left;
        if (pos.y < mi.rcWork.top)  pos.y = mi.rcWork.top;
    }

    SetWindowPos(hwnd, HWND_TOPMOST, pos.x, pos.y, winW, winH,
        SWP_NOACTIVATE);

    CreateControls(hwnd);

    ShowWindow(hwnd, SW_SHOW);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    RegisterHotKey(hwnd, kEscHotkeyId, MOD_NOREPEAT, VK_ESCAPE);

    MSG msg;
    while (!g_done) {
        if (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            break;
        }
    }

    UnregisterHotKey(hwnd, kEscHotkeyId);

    DestroyWindow(hwnd);
}

} // namespace wc
