/**
 * @file logGlobal.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 11-Jul-26
 */

#pragma once

#include <array>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#ifndef NDEBUG
#include <spdlog/sinks/stdout_color_sinks.h>
#endif
#include <filesystem>
#include <shlobj.h>
#include <vector>

constexpr std::array<const char *, 8> kLoggerNames = {
    "amwin", "enricher", "lastfm", "scraper", "imgur", "discord", "cache", "orchestrator"};

/**
 * Finds %appdata%/musicppv2/logs.
 * @return Path to logging dir.
 */
inline std::filesystem::path defaultLogDir() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        throw std::runtime_error("Failed to get LocalAppData folder");
    }
    const std::filesystem::path base(path);
    CoTaskMemFree(path);
    return base / "musicppv2" / "logs";
}

/**
 * Reads an environment variable from the user's config.
 * @param name Variable to read.
 * @return Value of var if it exists.
 */
inline std::optional<std::string> envVar(const char *name) {
    char *buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, name) == 0 && buf != nullptr) {
        std::string value(buf);
        free(buf);
        return value;
    }
    return std::nullopt;
}

/**
 * Default runtime log level from build configuration.
 * @return Log level.
 */
constexpr spdlog::level::level_enum defaultLevel() {
#ifdef NDEBUG
    return spdlog::level::info;
#else
    return spdlog::level::debug;
#endif
}

/**
 * Owns one-time spdlog setup.
 */
class Logging {
public:
    static void initialize() {
        volatile static Logging instance;
    }

    Logging(const Logging &) = delete;

    Logging &operator=(const Logging &) = delete;

    Logging(Logging &&) = delete;

    Logging &operator=(Logging &&) = delete;

private:
    Logging() {
        const std::filesystem::path logDir = defaultLogDir();
        std::error_code ec;
        create_directories(logDir, ec);
        if (ec) {
            throw std::runtime_error("Couldn't create log folder: " + ec.message());
        }
        const std::string logFile = (logDir / "musicpp.log").string();

        // 10 MiB per file, 3 rotated files.
        auto fileSink =
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(logFile, 10 * 1024 * 1024, 3);
        fileSink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] %v");
        // default logger pattern just in case

        std::vector<spdlog::sink_ptr> sinks{std::move(fileSink)};

#ifndef NDEBUG
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        consoleSink->set_pattern("%H:%M:%S.%e %^%-5l%$ %-12n %v");
        sinks.push_back(std::move(consoleSink));
#endif

        for (const char *name : kLoggerNames) {
            spdlog::register_logger(
                std::make_shared<spdlog::logger>(name, sinks.begin(), sinks.end()));
        }

        spdlog::level::level_enum level = defaultLevel();
        if (const auto value = envVar("MUSICPP_LOG_LEVEL")) {
            // from_str returns level::off for unknown names, so validate the round-trip
            if (const auto parsed = spdlog::level::from_str(*value);
                spdlog::level::to_string_view(parsed) == *value) {
                level = parsed;
            }
        }
        spdlog::set_level(level);

        spdlog::flush_on(spdlog::level::warn);
        spdlog::flush_every(std::chrono::seconds(3));
    }
};
