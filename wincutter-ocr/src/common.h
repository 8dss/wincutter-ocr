#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <unknwn.h>

#include <string>

namespace wc {

std::wstring Widen(const std::string& s);

std::string Narrow(const std::wstring& s);

void Log(const std::wstring& msg);

std::wstring Trim(const std::wstring& s);

std::wstring CollapseWhitespace(const std::wstring& s);

} // namespace wc
