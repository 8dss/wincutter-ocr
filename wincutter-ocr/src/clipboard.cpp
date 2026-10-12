#include "clipboard.h"

namespace wc {

bool CopyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(nullptr)) {
        Log(L"[clipboard] OpenClipboard failed: " + std::to_wstring(GetLastError()));
        return false;
    }

    EmptyClipboard();

    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!mem) {
        CloseClipboard();
        Log(L"[clipboard] GlobalAlloc failed: " + std::to_wstring(GetLastError()));
        return false;
    }

    void* dst = GlobalLock(mem);
    memcpy(dst, text.c_str(), bytes);
    GlobalUnlock(mem);

    if (!SetClipboardData(CF_UNICODETEXT, mem)) {
        GlobalFree(mem);
        CloseClipboard();
        Log(L"[clipboard] SetClipboardData failed: " + std::to_wstring(GetLastError()));
        return false;
    }

    CloseClipboard();
    return true;
}

} // namespace wc
