//
// Created by Jonathan on 17-Nov-25.
//

#include "stringutils.h"
#include <windows.h>
#include <vector>
#include <algorithm>
#include <wincrypt.h>

using namespace std;

string convertWString(const wstring &wstr) {
    if (wstr.empty()) {
        return "";
    }

    const int required_size = WideCharToMultiByte(
        CP_UTF8,
        0,
        wstr.data(),
        static_cast<int>(wstr.length()),
        nullptr,
        0,
        nullptr, nullptr
    );

    if (required_size <= 0) {
        return ""; // Conversion error
    }
    std::string narrow_str(required_size, '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        wstr.data(),
        static_cast<int>(wstr.length()),
        &narrow_str[0],
        required_size,
        nullptr, nullptr
    );

    return narrow_str;
}

std::string cleanAlbumName(const std::string &input) {
    auto pos = input.rfind(" — Single");

    if (pos == string::npos) {
        pos = input.rfind(" — EP");
        if (pos == string::npos) {
            return input;
        }
    }
    return input.substr(0, pos);
}

wstring convertToWString(const string &str) {
    if (str.empty()) {
        return L"";
    }

    int required_size = MultiByteToWideChar(CP_UTF8, 0, str.data(),
        static_cast<int>(str.length()), nullptr, 0);

    if (required_size <= 0) {
        return L"";
    }

    wstring wstr(required_size, L'\0');

    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.length()),
        &wstr[0], required_size);

    return wstr;
}

int levenshteinDistance(const string &s1, const string &s2) {
    const size_t len1 = s1.size();
    const size_t len2 = s2.size();
    vector<vector<int> > dp(len1 + 1, vector<int>(len2 + 1));

    for (size_t i = 0; i <= len1; i++) {
        dp[i][0] = i;
    }
    for (size_t j = 0; j <= len2; j++) {
        dp[0][j] = j;
    }

    for (size_t i = 1; i <= len1; i++) {
        for (size_t j = 1; j <= len2; j++) {
            int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            dp[i][j] = min({
                dp[i - 1][j] + 1, // deletion
                dp[i][j - 1] + 1, // insertion
                dp[i - 1][j - 1] + cost // substitution
            });
        }
    }

    return dp[len1][len2];
}

double calculateSimilarity(const string &str1, const string &str2) {
    if (str1.empty() && str2.empty()) return 100.0;
    if (str1.empty() || str2.empty()) return 0.0;

    string s1 = toLowerCase(str1);
    string s2 = toLowerCase(str2);

    double distance = levenshteinDistance(s1, s2);
    double maxLen = max(s1.length(), s2.length());

    return (maxLen - distance) / maxLen * 100.0;
}

string toLowerCase(const string &str) {
    string result = str;
    ranges::transform(result, result.begin(),
                      [](const unsigned char c) { return tolower(c); });
    return result;
}

std::string discord_bounds(const wstring &wstr, const string &fallback) {
    size_t len = wstr.length();
    if (len > 128) {
        string input = convertWString(wstr);
        auto l = input.begin();
        while (l != input.end() && isspace(*l)) ++l;
        auto r = input.end();
        do {
            if (r == l) {
                break;
            }
            --r;
        } while (isspace(*r));

        string out;
        if (r>=l) {
            out.assign (l, r+1);
        }
        if (out.size() > 128) {
            out.resize(128);
        }

        return out;
    }
    if (len <= 1) {
        return fallback;
    }
    return convertWString(wstr);
}

std::string sanitizeKeys(std::string input) {
    ranges::replace(input, '|', '-');
    return input;
}

std::string md5(const std::string& input) {
    HCRYPTPROV hProv = 0;
    HCRYPTHASH hHash = 0;
    BYTE digest[16];
    DWORD digestLen = 16;

    if (!CryptAcquireContext(&hProv, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) return "";
    if (!CryptCreateHash(hProv, CALG_MD5, 0, 0, &hHash)) {
        CryptReleaseContext(hProv, 0);
        return "";
    }

    CryptHashData(hHash, reinterpret_cast<const BYTE*>(input.data()), input.size(), 0);
    CryptGetHashParam(hHash, HP_HASHVAL, digest, &digestLen, 0);
    CryptDestroyHash(hHash);
    CryptReleaseContext(hProv, 0);

    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    out.reserve(32);

    for (const unsigned char b : digest) {
        out.push_back(hex[b>>4]);
        out.push_back(hex[b & 0x0F]);
    }

    return out;
}

const char* b(const bool v) { return v ? "1" : "0"; }