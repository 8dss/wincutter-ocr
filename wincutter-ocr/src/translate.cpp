#include "translate.h"

#include <winhttp.h>

#include <cstdio>

#pragma comment(lib, "winhttp.lib")

namespace wc {

namespace {

std::string PercentEncode(const std::string& in) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(in.size() * 3);
    for (unsigned char c : in) {
        bool unreserved =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0x0F]);
        }
    }
    return out;
}

void AppendUtf8(std::string& out, uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

uint32_t ParseHex4(const std::string& s, size_t pos) {
    uint32_t v = 0;
    for (int i = 0; i < 4 && pos + i < s.size(); ++i) {
        char c = s[pos + i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (c - '0');
        else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
    }
    return v;
}

std::string ReadJsonString(const std::string& json, size_t& i) {
    if (i >= json.size() || json[i] != '"') {
        return {};
    }
    ++i;
    std::string utf8;
    while (i < json.size() && json[i] != '"') {
        unsigned char c = static_cast<unsigned char>(json[i]);
        if (c == '\\' && i + 1 < json.size()) {
            char e = json[i + 1];
            switch (e) {
            case 'n': utf8.push_back('\n'); i += 2; break;
            case 't': utf8.push_back('\t'); i += 2; break;
            case 'r': utf8.push_back('\r'); i += 2; break;
            case '"': utf8.push_back('"');  i += 2; break;
            case '\\': utf8.push_back('\\'); i += 2; break;
            case '/': utf8.push_back('/');  i += 2; break;
            case 'b': utf8.push_back('\b'); i += 2; break;
            case 'f': utf8.push_back('\f'); i += 2; break;
            case 'u': {
                uint32_t cp = ParseHex4(json, i + 2);
                i += 6;
                if (cp >= 0xD800 && cp <= 0xDBFF && i + 5 < json.size() &&
                    json[i] == '\\' && json[i + 1] == 'u') {
                    uint32_t lo = ParseHex4(json, i + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        i += 6;
                    }
                }
                AppendUtf8(utf8, cp);
                break;
            }
            default: utf8.push_back(e); i += 2; break;
            }
        } else {
            utf8.push_back(static_cast<char>(c));
            ++i;
        }
    }
    if (i < json.size()) {
        ++i;
    }
    return utf8;
}

void SkipJsonString(const std::string& json, size_t& i) {
    if (i >= json.size() || json[i] != '"') {
        return;
    }
    ++i;
    while (i < json.size()) {
        if (json[i] == '\\') {
            i += 2;
            continue;
        }
        if (json[i] == '"') {
            ++i;
            return;
        }
        ++i;
    }
}

// Google отдаёт перевод длинного текста несколькими сегментами:
//   [[["часть1","original1",...],["часть2","original2",...],...],null,"en",...]
// Склеиваем переводы всех сегментов (но не оригиналы).
std::wstring ParseTranslation(const std::string& json) {
    size_t i = json.find("[[[");
    if (i == std::string::npos) {
        return {};
    }
    i += 3;

    std::string utf8;
    while (i < json.size()) {
        while (i < json.size() && (json[i] == ' ' || json[i] == '\n' ||
                                   json[i] == '\r' || json[i] == '\t')) {
            ++i;
        }
        if (i >= json.size() || json[i] != '"') {
            break;
        }
        utf8 += ReadJsonString(json, i);

        int depth = 1;
        while (i < json.size() && depth > 0) {
            if (json[i] == '"') {
                SkipJsonString(json, i);
                continue;
            }
            if (json[i] == '[') {
                ++depth;
            } else if (json[i] == ']') {
                --depth;
            }
            ++i;
        }

        while (i < json.size() && (json[i] == ',' || json[i] == ' ' ||
                                   json[i] == '\n' || json[i] == '\r' || json[i] == '\t')) {
            ++i;
        }
        if (i < json.size() && json[i] == '[') {
            ++i;
            continue;
        }
        break;
    }
    return Widen(utf8);
}

std::string HttpGet(const std::wstring& host, const std::wstring& path) {
    HINTERNET session = WinHttpOpen(L"WinCutter/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        Log(L"[http] WinHttpOpen failed: " + std::to_wstring(GetLastError()));
        return {};
    }

    std::string body;
    HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connect) {
        HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(),
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (request) {
            BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
            if (sent && WinHttpReceiveResponse(request, nullptr)) {
                DWORD available = 0;
                do {
                    available = 0;
                    if (!WinHttpQueryDataAvailable(request, &available) || available == 0) {
                        break;
                    }
                    std::string chunk(available, '\0');
                    DWORD read = 0;
                    if (!WinHttpReadData(request, chunk.data(), available, &read)) {
                        break;
                    }
                    chunk.resize(read);
                    body += chunk;
                } while (available > 0);
            } else {
                Log(L"[http] Запрос не удался: " + std::to_wstring(GetLastError()));
            }
            WinHttpCloseHandle(request);
        } else {
            Log(L"[http] WinHttpOpenRequest failed: " + std::to_wstring(GetLastError()));
        }
        WinHttpCloseHandle(connect);
    } else {
        Log(L"[http] WinHttpConnect failed: " + std::to_wstring(GetLastError()));
    }
    WinHttpCloseHandle(session);
    return body;
}

} // namespace

std::wstring Translate(const std::wstring& text, const wchar_t* from, const wchar_t* to) {
    std::string encoded = PercentEncode(Narrow(text));
    std::wstring path =
        L"/translate_a/single?client=gtx&dt=t&sl=" + std::wstring(from) +
        L"&tl=" + std::wstring(to) + L"&q=" + Widen(encoded);

    std::string body = HttpGet(L"translate.googleapis.com", path);
    if (body.empty()) {
        return {};
    }
    return ParseTranslation(body);
}

} // namespace wc
