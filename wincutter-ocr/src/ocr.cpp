#include "ocr.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include <cwctype>
#include <mutex>
#include <limits>

namespace wc {

namespace {

// Оценка "правдоподобности" текста распознавания. Движок, не знающий языка,
// часто выдаёт цифры вместо букв (например, кириллица -> "93blKe"), поэтому
// штрафуем цифры и поощряем буквы.
int ScoreText(const std::wstring& text) {
    int letters = 0;
    int digits = 0;
    for (wchar_t c : text) {
        if (std::iswalpha(c)) {
            ++letters;
        } else if (std::iswdigit(c)) {
            ++digits;
        }
    }
    return letters - 4 * digits;
}

void EnsureApartment() {
    static std::once_flag once;
    std::call_once(once, [] {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {
        }
    });
}

} // namespace

std::wstring OcrBgra(const uint8_t* pixels, int width, int height, const wchar_t* langTag) {
    using namespace winrt::Windows::Media::Ocr;
    using namespace winrt::Windows::Graphics::Imaging;
    using namespace winrt::Windows::Storage::Streams;
    using namespace winrt::Windows::Globalization;

    EnsureApartment();

    OcrEngine engine{ nullptr };
    if (langTag && *langTag) {
        engine = OcrEngine::TryCreateFromLanguage(Language{ langTag });
    }
    if (!engine) {
        engine = OcrEngine::TryCreateFromUserProfileLanguages();
    }
    if (!engine) {
        Log(L"[ocr] Не удалось создать OCR-движок. Установи языковой пакет OCR.");
        return {};
    }

    const uint32_t bytes = static_cast<uint32_t>(width) * static_cast<uint32_t>(height) * 4u;
    Buffer buffer{ bytes };
    memcpy(buffer.data(), pixels, bytes);
    buffer.Length(bytes);

    SoftwareBitmap bitmap = SoftwareBitmap::CreateCopyFromBuffer(
        buffer, BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Ignore);

    OcrResult result{ nullptr };
    try {
        result = engine.RecognizeAsync(bitmap).get();
    } catch (winrt::hresult_error const& e) {
        Log(L"[ocr] Ошибка распознавания: " + std::wstring(e.message()));
        return {};
    }

    return std::wstring(result.Text().c_str());
}

std::wstring OcrBgraAuto(const uint8_t* pixels, int width, int height) {
    auto langs = OcrAvailableLanguages();
    if (langs.empty()) {
        return OcrBgra(pixels, width, height, L"en-US");
    }

    std::wstring best;
    int bestScore = std::numeric_limits<int>::min();
    for (auto const& lang : langs) {
        std::wstring result = OcrBgra(pixels, width, height, lang.c_str());
        int score = ScoreText(result);
        if (score > bestScore) {
            bestScore = score;
            best = std::move(result);
        }
    }
    return best;
}

std::vector<std::wstring> OcrAvailableLanguages() {
    using namespace winrt::Windows::Media::Ocr;
    using namespace winrt::Windows::Globalization;

    EnsureApartment();

    std::vector<std::wstring> langs;
    try {
        auto const available = OcrEngine::AvailableRecognizerLanguages();
        uint32_t const count = available.Size();
        for (uint32_t i = 0; i < count; ++i) {
            langs.push_back(std::wstring{ available.GetAt(i).LanguageTag().c_str() });
        }
    } catch (winrt::hresult_error const& e) {
        Log(L"[ocr] Ошибка перечисления языков: " + std::wstring(e.message()));
    }
    return langs;
}

} // namespace wc
