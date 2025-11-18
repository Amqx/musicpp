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

bool is_extended_whitespace(wchar_t ch) {
    if (ch == L' ' || ch == L'\t' || ch == L'\n' || ch == L'\r' || ch == L'\f' || ch == L'\v') {
        return true;
    }
    if (ch == 0x00A0 || ch == 0x2007 || ch == 0x202F) {
        return true;
    }
    if (ch >= 0x2000 && ch <= 0x200A) {
        return true;
    }
    return false;
}

wstring trim_copy(const wstring &value) {
    if (value.empty()) {
        return L"";
    }

    size_t first = 0;
    while (first < value.size() && is_extended_whitespace(value[first])) {
        ++first;
    }
    if (first == value.size()) {
        return L"";
    }

    size_t last = value.size() - 1;
    while (last > first && is_extended_whitespace(value[last])) {
        --last;
    }
    return value.substr(first, last - first + 1);
}

bool split_artist_album(const wstring &input, wstring &outArtist, wstring &outAlbum) {
    auto is_dash = [](wchar_t ch) {
        return ch == L'-' || ch == 0x2013 || ch == 0x2014 || ch == 0x2015 || ch == 0x2212;
    };

    for (size_t dashPos = 0; dashPos < input.size(); ++dashPos) {
        wchar_t dashChar = input[dashPos];
        if (!is_dash(dashChar)) {
            continue;
        }

        size_t artistEnd = dashPos;
        while (artistEnd > 0 && is_extended_whitespace(input[artistEnd - 1])) {
            --artistEnd;
        }

        size_t albumStart = dashPos + 1;
        while (albumStart < input.size()) {
            wchar_t ch = input[albumStart];
            if (ch == dashChar || is_extended_whitespace(ch)) {
                ++albumStart;
            } else {
                break;
            }
        }

        wstring candidateArtist = trim_copy(input.substr(0, artistEnd));
        wstring candidateAlbum = trim_copy(albumStart < input.size() ? input.substr(albumStart) : L"");

        if (!candidateArtist.empty() && !candidateAlbum.empty()) {
            outArtist = std::move(candidateArtist);
            outAlbum = std::move(candidateAlbum);
            return true;
        }
    }

    outArtist.clear();
    outAlbum.clear();
    return false;
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