#pragma once
#include <Windows.h>

// encoding function
std::string to_utf8(const std::wstring& wide_string)
{
    if (wide_string.empty()) return std::string();
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wide_string[0], (int)wide_string.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wide_string[0], (int)wide_string.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

// decoding function
std::wstring from_utf8(const std::string& utf8_string)
{
    if (utf8_string.empty()) return std::wstring();
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, &utf8_string[0], (int)utf8_string.size(), NULL, 0);
    std::wstring wstrTo(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, &utf8_string[0], (int)utf8_string.size(), &wstrTo[0], size_needed);
    return wstrTo;
}