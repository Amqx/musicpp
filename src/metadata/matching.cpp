/**
 * @file matching.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 26-Jun-26
 */

#include "metadata/matching.hpp"

#include <algorithm>
#include <vector>

namespace {
std::string toLowerCase(const std::string &str) {
    std::string result = str;
    std::ranges::transform(result, result.begin(),
                           [](const unsigned char c) { return tolower(c); });
    return result;
}

int levenshteinDistance(const std::string &s1, const std::string &s2) {
    const size_t len1 = s1.size();
    const size_t len2 = s2.size();
    std::vector dp(len1 + 1, std::vector<int>(len2 + 1));

    for (size_t i = 0; i <= len1; i++) {
        dp[i][0] = i;
    }
    for (size_t j = 0; j <= len2; j++) {
        dp[0][j] = j;
    }

    for (size_t i = 1; i <= len1; i++) {
        for (size_t j = 1; j <= len2; j++) {
            const int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;
            dp[i][j] = std::min({
                dp[i - 1][j] + 1, // deletion
                dp[i][j - 1] + 1, // insertion
                dp[i - 1][j - 1] + cost // substitution
            });
        }
    }

    return dp[len1][len2];
}

// Operates on strings already lowercased by the caller, so the common path lowercases only once.
double similarityRatio(const std::string &s1, const std::string &s2) {
    if (s1.empty() && s2.empty())
        return 100.0;
    if (s1.empty() || s2.empty())
        return 0.0;

    const double distance = levenshteinDistance(s1, s2);
    const double max_len = std::ranges::max(s1.length(), s2.length());

    return (max_len - distance) / max_len * 100.0;
}

// A source routinely appends decoration a player omits ("Bohemian Rhapsody (Remastered 2011)"),
// which the whole-string ratio counts entirely against the match. When one string contains the
// other, treat it as a match — but only past a length floor, so a short needle can't be pulled in.
constexpr size_t kSubstringFloor = 5;

// Operates on strings already lowercased by the caller.
bool substringMatch(const std::string &s1, const std::string &s2) {
    if (s1.length() < kSubstringFloor || s2.length() < kSubstringFloor)
        return false;

    return s1.find(s2) != std::string::npos || s2.find(s1) != std::string::npos;
}

}

bool fuzzyMatch(const std::string &a, const std::string &b, const bool allowSubstring) {
    // Lowercase once here; both the ratio and the substring check work on the lowered forms.
    const std::string la = toLowerCase(a);
    const std::string lb = toLowerCase(b);

    return similarityRatio(la, lb) >= kMatchGenerosity ||
           (allowSubstring && substringMatch(la, lb));
}