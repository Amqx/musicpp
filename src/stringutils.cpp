//
// Created by Jonathan on 17-Nov-25.
//

#include "stringutils.h"
#include <windows.h>
#include <vector>
#include <algorithm>

using namespace std;

string convertWString(const wstring &wstr) {
    if (wstr.empty()) {
        return "";
    }

    const int required_size = WideCharToMultiByte(
        CP_UTF8, // CodePage: Convert to UTF-8
        0, // dwFlags
        wstr.data(), // lpWideCharStr: Pointer to wide string data
        static_cast<int>(wstr.length()), // cchWideChar: Length of the wide string (excluding null)
        nullptr, // lpMultiByteStr: Output buffer (NULL to get size)
        0, // cbMultiByte: Output buffer size (0 to get size)
        nullptr, nullptr // lpDefaultChar, lpUsedDefaultChar
    );

    if (required_size <= 0) {
        return ""; // Conversion error
    }
    std::string narrow_str(required_size, '\0');

    WideCharToMultiByte(
        CP_UTF8, // CodePage: Convert to UTF-8
        0, // dwFlags
        wstr.data(), // lpWideCharStr
        static_cast<int>(wstr.length()), // cchWideChar
        &narrow_str[0], // lpMultiByteStr: Use the internal buffer (C++11+)
        required_size, // cbMultiByte: Size of the buffer
        nullptr, nullptr
    );

    return narrow_str;
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
    std::replace(input.begin(), input.end(), '|', '-');
    return input;
}