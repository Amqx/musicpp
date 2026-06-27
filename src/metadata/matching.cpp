/**
 * @file matching.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 26-Jun-26
 */

#include "metadata/matching.hpp"

#include <algorithm>
#include <vector>

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

double calculateSimilarity(const std::string &str1, const std::string &str2) {
    if (str1.empty() && str2.empty()) return 100.0;
    if (str1.empty() || str2.empty()) return 0.0;

    const std::string s1 = toLowerCase(str1);
    const std::string s2 = toLowerCase(str2);

    const double distance = levenshteinDistance(s1, s2);
    const double max_len = std::ranges::max(s1.length(), s2.length());

    return (max_len - distance) / max_len * 100.0;
}

bool fuzzyMatch(const std::string &a, const std::string &b) {
    return calculateSimilarity(a, b) >= kMatchGenerosity;
}
