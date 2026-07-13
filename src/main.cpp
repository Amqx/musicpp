/**
 * @file main.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#include "types/track.hpp"
#include "system/amwin.hpp"
#include "metadata/cache.hpp"
#include "metadata/enricher.hpp"
#include "metadata/sources/lastfm.hpp"
#include "metadata/sources/scraper.hpp"
#include "metadata/uploaders/imgur.hpp"
#include "log/log.hpp"
#include "orchestrator/orchestrator.hpp"

#include <csignal>
#include <memory>
#include <discord/rp.hpp>
#include <windows.h>

namespace {
    std::atomic running{true};
std::mutex sleep_mutex;
std::condition_variable sleep_cv;

BOOL WINAPI consoleCtrlHandler(const DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT ||
        ctrlType == CTRL_CLOSE_EVENT) {
        logging::get("")->info("Interrupt received. Shutting down...");
        running.store(false);
        sleep_cv.notify_all();
        return TRUE;
    }
    return FALSE;
}

}

int main() {
    if (!SetConsoleCtrlHandler(consoleCtrlHandler, TRUE)) {
        logging::get("")->warn("Failed to set up signal handler.");
    }
    logging::init();

    MetadataCache cache;
    Orchestrator orchestrator{};

    auto enricher = std::make_unique<Enricher>(cache);
    enricher->registerSource(std::make_shared<Scraper>("ca"));
    // Redacted
    if (std::string imgurId = ""; !imgurId.empty()) {
        enricher->registerUploader(std::make_unique<Imgur>(imgurId));
    }

    // Redacted
    if (std::string lastfmKey = "", lastfmSecret =
                "";
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

    while (running) {
        orchestrator.run(); {
            std::unique_lock lock(sleep_mutex);
            sleep_cv.wait_for(lock, std::chrono::seconds(5), [] {
                return !running.load();
            });
        }
    }

    return 0;
}
