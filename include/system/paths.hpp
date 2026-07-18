/**
 * @file paths.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 17-Jul-26
 */

/**
 * Resolves the directories musicpp stores its state in.
 */

#pragma once

#include <filesystem>

/**
 * Finds %LOCALAPPDATA%/musicppv2.
 * @return Path to the app's data directory. Not created if missing.
 */
std::filesystem::path appDataDir();
