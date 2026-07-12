/**
 * @file matching.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 26-Jun-26
 */

#pragma once

#include <string>

constexpr double kMatchGenerosity = 60.0; // Minimum similarity percentage (0-100) for a fuzzy match

bool fuzzyMatch(const std::string &a, const std::string &b);