/**
* @file paths.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 17-Jul-26
 */

#include <memory>
#include <shlobj.h>
#include <stdexcept>
#include <filesystem>

namespace {
struct CoTaskMemDeleter {
    void operator()(wchar_t *p) const noexcept {
        CoTaskMemFree(p);
    }
};
}

std::filesystem::path appDataDir() {
    wchar_t *raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
        throw std::runtime_error("Failed to get LocalAppData folder");
    }
    const std::unique_ptr<wchar_t, CoTaskMemDeleter> path(raw);
    return std::filesystem::path(path.get()) / "musicppv2";
}
