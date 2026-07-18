/**
 * @file matching.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 26-Jun-26
 */

#pragma once

#include <string>

constexpr double kMatchGenerosity = 60.0; // Minimum similarity percentage (0-100) for a fuzzy match

/**
 * Whether two strings name the same thing, tolerant of case, punctuation and small typos.
 * @param a First string.
 * @param b Second string.
 * @param allowSubstring When true, one string wholly containing the other (past a short length
 * floor) also counts as a match. Reserved for fields that carry source-appended decoration such as
 * titles ("… (Remastered 2011)").
 * @return Whether the two are considered a match.
 */
[[nodiscard]] bool fuzzyMatch(const std::string &a, const std::string &b,
                              bool allowSubstring = false);