#include "common.h"
#include "capture.h"
#include "clipboard.h"
#include "ocr.h"
#include "overlay.h"
#include "result_window.h"
#include "translate.h"
#include "tray.h"

#include <shellapi.h>
#include <cstdio>

namespace wc {

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), r.data(), n, nullptr, nullptr);
    return r;
}

void Log(const std::wstring& msg) {
    std::wstring line = msg + L"\r\n";
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out && GetConsoleMode(out, &mode)) {
        DWORD written = 0;
        WriteConsoleW(out, line.c_str(), static_cast<DWORD>(line.size()), &written, nullptr);
    } else {
        std::string utf8 = Narrow(line);
        DWORD written = 0;
        if (out) {
            WriteFile(out, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        }
    }
}

std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    auto isws = [](wchar_t c) { return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n'; };
    while (a < b && isws(s[a])) ++a;
    while (b > a && isws(s[b - 1])) --b;
    return s.substr(a, b - a);
}

std::wstring CollapseWhitespace(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size());
    bool prevWs = false;
    for (wchar_t c : s) {
        bool ws = (c == L' ' || c == L'\t' || c == L'\r' || c == L'\n');
        if (ws) {
            if (!prevWs && !out.empty()) out.push_back(L' ');
            prevWs = true;
        } else {
            out.push_back(c);
            prevWs = false;
        }
    }
    return Trim(out);
}

} // namespace wc

namespace {

const int kHotkeyId = 1;

void EnableDpiAwareness() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
}

void LogSeparator() {
    wc::Log(L"----------------------------------------");
}

void TranslateAndCopy(const std::wstring& text) {
    using namespace wc;
    std::wstring src = CollapseWhitespace(text);
    if (src.empty()) {
        Log(L"Пустой текст.");
        return;
    }
    LogSeparator();
    Log(L"Текст: " + src);

    std::wstring translated = Translate(src, L"auto", L"ru");
    if (translated.empty()) {
        Log(L"Перевод не получен (проверь интернет/язык).");
        return;
    }
    Log(L"Перевод: " + translated);

    std::wstring out = src + L" - " + translated;
    CopyToClipboard(out);
}

void RunSelectionPipeline(HINSTANCE hInst) {
    using namespace wc;
    static bool busy = false;
    if (busy) {
        return;
    }
    busy = true;
    struct Reset { bool& b; ~Reset() { b = false; } } reset{ busy };

    auto rect = RunSelectionOverlay(hInst);
    if (!rect) {
        return;
    }
    POINT anchor{ rect->left, rect->bottom };

    ImageBgra img;
    if (!CaptureScreenRect(*rect, img)) {
        return;
    }

    std::wstring recognized = OcrBgraAuto(img.pixels.data(), img.width, img.height);
    std::wstring src = CollapseWhitespace(recognized);
    if (src.empty()) {
        ShowResultWindow(hInst, L"(текст не распознан)", L"", anchor);
        return;
    }

    std::wstring translated = Translate(src, L"auto", L"ru");
    if (!translated.empty()) {
        CopyToClipboard(src + L" - " + translated);
    } else {
        translated = L"(перевод не получен)";
    }
    ShowResultWindow(hInst, src, translated, anchor);
}

void CaptureFullScreen(HINSTANCE hInst) {
    using namespace wc;
    RECT r{ 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) };

    ImageBgra img;
    if (!CaptureScreenRect(r, img)) {
        Log(L"Не удалось снять экран.");
        return;
    }

    std::wstring recognized = OcrBgraAuto(img.pixels.data(), img.width, img.height);
    TranslateAndCopy(recognized);
}

void OcrSelfTest() {
    using namespace wc;
    const int W = 900, H = 240;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = W;
    bmi.bmiHeader.biHeight = -H;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    HDC screen = GetDC(nullptr);
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    RECT full{ 0, 0, W, H };
    FillRect(mem, &full, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, RGB(0, 0, 0));
    HFONT font = CreateFontW(-30, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ oldFont = SelectObject(mem, font);

    const wchar_t* en = L"Hello world. The quick brown fox jumps over the lazy dog.";
    const wchar_t* ru = L"Привет мир. Баланс между работой и личной жизнью.";
    RECT r1{ 24, 30, W - 24, 110 };
    DrawTextW(mem, en, -1, &r1, DT_LEFT | DT_TOP | DT_WORDBREAK);
    RECT r2{ 24, 130, W - 24, 210 };
    DrawTextW(mem, ru, -1, &r2, DT_LEFT | DT_TOP | DT_WORDBREAK);
    GdiFlush();

    std::string report;
    for (auto const& lang : OcrAvailableLanguages()) {
        std::wstring out = OcrBgra(static_cast<const uint8_t*>(bits), W, H, lang.c_str());
        std::wstring line = L"[ " + lang + L" ] " + out;
        Log(line);
        report += Narrow(line) + "\n\n";
    }
    {
        std::wstring autoOut = OcrBgraAuto(static_cast<const uint8_t*>(bits), W, H);
        std::wstring line = L"[ auto ] " + autoOut;
        Log(line);
        report += Narrow(line) + "\n\n";
    }
    {
        HANDLE f = CreateFileW(L"D:\\WinCutter\\build\\ocr_selftest.txt",
            GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(f, report.data(), static_cast<DWORD>(report.size()), &written, nullptr);
            CloseHandle(f);
        }
    }

    SelectObject(mem, oldFont);
    SelectObject(mem, oldBmp);
    DeleteObject(font);
    DeleteDC(mem);
    DeleteObject(bmp);
    ReleaseDC(nullptr, screen);
}

void PrintHelp() {
    wc::Log(L"WinCutter — переводчик текста с экрана (OCR → перевод)");
    wc::Log(L"");
    wc::Log(L"  WinCutter                запуск в фоне; хоткей — первое свободное");
    wc::Log(L"                           из Win+Shift+A/D/X, Ctrl+Shift+A (точное — в трее)");
    wc::Log(L"  WinCutter --capture      снять весь экран, распознать, перевести");
    wc::Log(L"  WinCutter --translate X  перевести строку X");
    wc::Log(L"  WinCutter --probe        показать свободные сочетания клавиш");
    wc::Log(L"  WinCutter --langs        список доступных языков распознавания (OCR)");
    wc::Log(L"  WinCutter --demo         показать окно результата (проверка)");
    wc::Log(L"  WinCutter --help         эта справка");
}

void ProbeHotkeys() {
    struct Combo { const wchar_t* name; UINT mods; };
    const Combo combos[] = {
        { L"Win+Shift", MOD_WIN | MOD_SHIFT },
        { L"Ctrl+Shift", MOD_CONTROL | MOD_SHIFT },
        { L"Alt+Shift", MOD_ALT | MOD_SHIFT },
        { L"Ctrl+Alt", MOD_CONTROL | MOD_ALT },
    };
    int id = 100;
    for (const auto& c : combos) {
        std::wstring freeKeys;
        for (wchar_t ch = L'A'; ch <= L'Z'; ++ch) {
            if (RegisterHotKey(nullptr, id, c.mods | MOD_NOREPEAT, ch)) {
                UnregisterHotKey(nullptr, id);
                freeKeys.push_back(ch);
                freeKeys.push_back(L' ');
            }
            ++id;
        }
        wc::Log(std::wstring(L"[свободно] ") + c.name + L"+ : " + freeKeys);
    }
}

const UINT kTrayMsg = WM_APP + 1;
const int IDM_CAPTURE = 1;
const int IDM_EXIT = 2;
const wchar_t* kHiddenClass = L"WinCutterHidden";

HINSTANCE g_hInstMain = nullptr;

LRESULT CALLBACK HiddenProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_HOTKEY:
        if (wp == kHotkeyId) {
            RunSelectionPipeline(g_hInstMain);
        }
        return 0;

    case kTrayMsg: {
        UINT e = LOWORD(lp);
        if (e == WM_RBUTTONUP || e == WM_CONTEXTMENU) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, IDM_CAPTURE, L"Перевести область");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Выход");
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            PostMessageW(hwnd, WM_NULL, 0, 0);
            DestroyMenu(menu);
        } else if (e == WM_LBUTTONDBLCLK) {
            RunSelectionPipeline(g_hInstMain);
        }
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_CAPTURE:
            RunSelectionPipeline(g_hInstMain);
            return 0;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            return 0;
        }
        return 0;

    case WM_DESTROY:
        wc::TrayRemove(hwnd);
        UnregisterHotKey(hwnd, kHotkeyId);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int RunHotkeyMode(HINSTANCE hInst) {
    g_hInstMain = hInst;

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = HiddenProc;
    wc.hInstance = hInst;
    wc.lpszClassName = kHiddenClass;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, kHiddenClass, L"WinCutter", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) {
        MessageBoxW(nullptr, L"Не удалось создать окно приложения.",
            L"WinCutter", MB_OK | MB_ICONERROR);
        return 1;
    }

    struct Hotkey { UINT mods; UINT vk; const wchar_t* label; };
    const Hotkey candidates[] = {
        { MOD_WIN | MOD_SHIFT,     'A', L"Win+Shift+A" },
        { MOD_WIN | MOD_SHIFT,     'D', L"Win+Shift+D" },
        { MOD_WIN | MOD_SHIFT,     'X', L"Win+Shift+X" },
        { MOD_CONTROL | MOD_SHIFT, 'A', L"Ctrl+Shift+A" },
        { MOD_CONTROL | MOD_ALT,   'A', L"Ctrl+Alt+A" },
    };

    std::wstring active;
    for (const auto& c : candidates) {
        if (RegisterHotKey(hwnd, kHotkeyId, c.mods | MOD_NOREPEAT, c.vk)) {
            active = c.label;
            break;
        }
    }
    if (active.empty()) {
        MessageBoxW(nullptr,
            L"Не удалось зарегистрировать хоткей: все сочетания заняты.\n"
            L"Запусти WinCutter --probe, чтобы найти свободное.",
            L"WinCutter", MB_OK | MB_ICONERROR);
        DestroyWindow(hwnd);
        return 1;
    }

    wc::TrayAdd(hwnd, hInst, kTrayMsg, L"WinCutter — хоткей " + active);
    wc::TrayBalloon(hwnd, L"WinCutter запущен",
        L"Хоткей: " + active + L"\nПравый клик по иконке — меню, двойной — перевести область.");

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}

void EnsureConsoleForCli() {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        return;
    }
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        HANDLE con = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (con != INVALID_HANDLE_VALUE) {
            SetStdHandle(STD_OUTPUT_HANDLE, con);
        }
        SetConsoleOutputCP(CP_UTF8);
    }
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    EnableDpiAwareness();

    HINSTANCE hInst = hInstance;

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argv && argc >= 2) {
        std::wstring arg = argv[1];
        EnsureConsoleForCli();

        if (arg == L"--help" || arg == L"-h") {
            PrintHelp();
            return 0;
        }
        if (arg == L"--capture") {
            CaptureFullScreen(hInst);
            return 0;
        }
        if (arg == L"--translate" && argc >= 3) {
            TranslateAndCopy(argv[2]);
            return 0;
        }
        if (arg == L"--probe") {
            ProbeHotkeys();
            return 0;
        }
        if (arg == L"--ocr-selftest") {
            OcrSelfTest();
            return 0;
        }
        if (arg == L"--langs") {
            auto langs = wc::OcrAvailableLanguages();
            if (langs.empty()) {
                wc::Log(L"Языки OCR не найдены. Установи языковой пакет (Параметры -> Время и язык).");
            } else {
                wc::Log(L"Доступные языки OCR:");
                for (auto const& l : langs) {
                    wc::Log(L"  " + l);
                }
            }
            return 0;
        }
        if (arg == L"--demo") {
            POINT p{ 300, 260 };
            wc::ShowResultWindow(hInst,
                L"This is a demo sentence for the result window.",
                L"Это демонстрационное предложение для окна результата.",
                p);
            return 0;
        }
        wc::Log(L"Неизвестный аргумент. WinCutter --help");
        return 1;
    }

    if (argv) {
        LocalFree(argv);
    }

    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\WinCutter.SingleInstance");
    if (single && GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(nullptr,
            L"WinCutter уже запущен и работает в фоне.\n"
            L"Не запускайте вторую копию — она займёт чужой хоткей.",
            L"WinCutter", MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    return RunHotkeyMode(hInst);
}
