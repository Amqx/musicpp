/**
 * @file main.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#include "types/track.hpp"
#include "players/amwin.hpp"
#include "metadata/cache.hpp"
#include "metadata/enricher.hpp"
#include "metadata/sources/lastfm.hpp"
#include "metadata/sources/scraper.hpp"
#include "metadata/uploaders/imgur.hpp"
#include "log/log.hpp"
#include "orchestrator/orchestrator.hpp"

#include <csignal>
#include <memory>
#include <string>
#include <discord/rp.hpp>
#include <windows.h>

#include <thread>
#include "ui/tray.hpp"

// For debug/ dev builds using environment variables for setup
#include <filesystem>
#include <fstream>

namespace {
std::atomic running{true};
std::mutex sleep_mutex;
std::condition_variable sleep_cv;

std::shared_ptr<Tray> g_tray = nullptr;
std::mutex g_tray_mutex;

BOOL WINAPI consoleCtrlHandler(const DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT ||
        ctrlType == CTRL_CLOSE_EVENT) {
        logging::get("main")->info("Interrupt received. Shutting down...");
        running.store(false);
        sleep_cv.notify_all();
        std::shared_ptr<Tray> local_tray;
        {
            std::lock_guard lock(g_tray_mutex);
            local_tray = g_tray;
        }
        if (local_tray) {
            local_tray -> requestQuit();
        }
        return TRUE;
    }
    return FALSE;
}

/// Strip leading/trailing ASCII whitespace and one layer of matching quotes.
std::string trim(std::string s) {
    constexpr auto ws = " \t\r\n";
    const auto first = s.find_first_not_of(ws);
    if (first == std::string::npos) {
        return "";
    }
    s = s.substr(first, s.find_last_not_of(ws) - first + 1);
    if (s.size() >= 2 && (s.front() == '"' || s.front() == '\'') && s.back() == s.front()) {
        s = s.substr(1, s.size() - 2);
    }
    return s;
}

/// Parse a `KEY=VALUE` .env file into a map.
std::unordered_map<std::string, std::string> loadDotEnv(const std::filesystem::path &path) {
    std::unordered_map<std::string, std::string> vars;
    std::ifstream file(path);
    if (!file) {
        return vars;
    }
    std::string line;
    while (std::getline(file, line)) {
        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }
        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        vars[trim(trimmed.substr(0, eq))] = trim(trimmed.substr(eq + 1));
    }
    return vars;
}

/// Look up an API key by name.
std::string apiKey(const std::string &name) {
    if (const char *v = std::getenv(name.c_str()); v && *v) {
        return v;
    }
    static const auto dotEnv = [] {
        for (const auto &candidate : {".env", "../.env"}) {
            if (auto vars = loadDotEnv(candidate); !vars.empty()) {
                return vars;
            }
        }
        return std::unordered_map<std::string, std::string>{};
    }();
    if (const auto it = dotEnv.find(name); it != dotEnv.end()) {
        return it->second;
    }
    return "";
}

}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
#ifndef NDEBUG
    // Debug builds keep the streaming console the log's console sink writes to.
    if (AllocConsole()) {
        FILE *f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        freopen_s(&f, "CONIN$", "r", stdin);
    }
    if (!SetConsoleCtrlHandler(consoleCtrlHandler, TRUE)) {
        logging::get("main")->warn("Failed to set up signal handler.");
    }
#endif
    logging::init();

    MetadataCache cache;
    Orchestrator orchestrator{};

    auto enricher = std::make_unique<Enricher>(cache);
    enricher->registerSource(std::make_shared<Scraper>("ca"));
    if (std::string imgurId = apiKey("IMGUR_KEY"); !imgurId.empty()) {
        enricher->registerUploader(std::make_unique<Imgur>(imgurId));
    }

    if (std::string lastfmKey = apiKey("LASTFM_KEY"), lastfmSecret = apiKey("LASTFM_SECRET");
        !lastfmKey.empty() && !lastfmSecret.empty()) {
        auto lastfm = std::make_shared<LastFm>(lastfmKey, lastfmSecret);
        if (!lastfm->authed() && !lastfm->authenticateUser()) {
            logging::get("lastfm")->warn("Authentication failed, scrobbling is disabled");
        }
        enricher->registerSource(lastfm);
        orchestrator.registerScrobbler(std::move(lastfm));
    }

    orchestrator.registerEnricher(std::move(enricher));

    auto discord = std::make_unique<RichPresence>(1358389458956976128);
    orchestrator.registerRichPresence(std::move(discord));

    auto applemusic = std::make_unique<AmWin>();
    orchestrator.registerPoller(std::move(applemusic));

    // The tray lives on this (UI) thread; the poll loop runs on the worker below.
    auto tray = std::make_shared<Tray>("MusicPPTray");
    {
        std::lock_guard lock(g_tray_mutex);
        g_tray = tray;
    }

    // Callback only fires within the tray's own menu, so always alive when this runs.
    tray->addMenuItem("Exit", [weak_tray = std::weak_ptr(tray)] {
        logging::get("main")->info("Exit selected. Shutting down...");
        running.store(false);
        sleep_cv.notify_all();
        if (const auto tray_ptr = weak_tray.lock()) {
            tray_ptr->requestQuit();
        }
    });

    std::jthread worker([sleep_mut = &sleep_mutex, sleep_cond = &sleep_cv, &orchestrator, tray] {
        while (running.load()) {
            orchestrator.run();
            tray->setTooltip(orchestrator.nowPlaying());
            std::unique_lock lock(*sleep_mut);
            sleep_cond->wait_for(lock, std::chrono::seconds(5), [&] { return !running.load(); });
        }
    });

    tray->runMessageLoop();

    // Loop ended
    {
        std::lock_guard lock(g_tray_mutex);
        g_tray = nullptr;
    }
    running.store(false);
    sleep_cv.notify_all();
    return 0;
}
