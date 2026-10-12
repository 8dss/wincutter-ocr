#include "capture.h"

namespace wc {

bool CaptureScreenRect(const RECT& rect, ImageBgra& out) {
    int w = rect.right - rect.left;
    int h = rect.bottom - rect.top;
    if (w <= 0 || h <= 0) {
        return false;
    }

    HDC screen = GetDC(nullptr);
    if (!screen) {
        return false;
    }

    HDC mem = CreateCompatibleDC(screen);
    if (!mem) {
        ReleaseDC(nullptr, screen);
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) {
        if (bmp) DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return false;
    }

    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    BOOL ok = BitBlt(mem, 0, 0, w, h, screen, rect.left, rect.top, SRCCOPY);

    if (ok) {
        size_t bytes = static_cast<size_t>(w) * static_cast<size_t>(h) * 4;
        out.width = w;
        out.height = h;
        out.pixels.assign(static_cast<const uint8_t*>(bits), static_cast<const uint8_t*>(bits) + bytes);
    }

    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    return ok == TRUE;
}

} // namespace wc
