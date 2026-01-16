//
// Created by Jonathan on 17-Nov-25.
//

#include <string>
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include "utils.h"

using namespace std;

size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    static_cast<string *>(userp)->append(static_cast<char *>(contents), size * nmemb);
    return size * nmemb;
}

string UrlEncode(const string &value, spdlog::logger *logger) {
    CURL *curl = curl_easy_init();
    if (!curl) {
        if (logger) {
            logger->warn("Failed to initialize CURL for urlEncode.");
        }
        return "";
    }
    char *output = curl_easy_escape(curl, value.c_str(), value.length());
    string encoded = output ? output : "";
    curl_free(output);
    curl_easy_cleanup(curl);
    return encoded;
}

bool IsHexChar(const wchar_t c) {
    return (c >= L'0' && c <= L'9') ||
           (c >= L'a' && c <= L'f') ||
           (c >= L'A' && c <= L'F');
}

bool LooksLikeHexKey(const std::wstring &s, const size_t expected_len) {
    if (s.length() != expected_len)
        return false;

    for (const wchar_t c: s) {
        if (!IsHexChar(c)) return false;
        if (iswspace(c)) return false;
    }

    return true;
}

bool LooksLikeUrlOrJson(const std::wstring &s) {
    return s.find(L"http") != std::wstring::npos ||
           s.find(L"{") != std::wstring::npos ||
           s.find(L"}") != std::wstring::npos ||
           s.find(L":") != std::wstring::npos;
}

bool ValidateInput(const std::wstring &value, const ValidationInputType type) {
    if (value.empty()) return false;
    if (LooksLikeUrlOrJson(value)) return false;
    return LooksLikeHexKey(value, type);
}
