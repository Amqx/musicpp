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
#include <memory>
#include <shlobj.h>
#include <stdexcept>

namespace paths {

namespace detail {
struct CoTaskMemDeleter {
    void operator()(wchar_t *p) const noexcept {
        CoTaskMemFree(p);
    }
};
}

/**
 * Finds %LOCALAPPDATA%/musicppv2.
 * @return Path to the app's data directory. Not created if missing.
 */
inline std::filesystem::path appDataDir() {
    wchar_t *raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        throw std::runtime_error("Failed to get LocalAppData folder");
    }
    const std::unique_ptr<wchar_t, detail::CoTaskMemDeleter> path(raw);
    return std::filesystem::path(path.get()) / "musicppv2";
}

}
