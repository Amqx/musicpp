/**
 * @file main.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#include "types/track.hpp"
#include "system/amwin.hpp"
#include "metadata/cache.hpp"
#include "metadata/enricher.hpp"
#include "metadata/sources/scraper.hpp"
#include "metadata/uploaders/imgur.hpp"
#include "log/log.hpp"

#include <csignal>
#include <memory>
#include <discord/rp.hpp>
#include <windows.h>

std::atomic running{true};
std::mutex sleep_mutex;
std::condition_variable sleep_cv;

BOOL WINAPI consoleCtrlHandler(const DWORD ctrlType) {
    if (ctrlType == CTRL_C_EVENT || ctrlType == CTRL_BREAK_EVENT || CTRL_CLOSE_EVENT) {
        logging::get("")->info("Interrupt received. Shutting down...");
        running.store(false);
        sleep_cv.notify_all();
        return TRUE;
    }
    return FALSE;
}

int main() {
    if (!SetConsoleCtrlHandler(consoleCtrlHandler, TRUE)) {
        logging::get("")->warn("Failed to set up signal handler.");
    }
    logging::init();
    const auto log = logging::get("orchestrator");

    auto applemusic = AmWin();
    const auto discord = RichPresence(1358389458956976128);
    MetadataCache cache;

    Enricher enricher(cache);
    enricher.registerSource(std::make_shared<Scraper>("ca"));

    // Redacted
    if (std::string imgurId = ""; !imgurId.empty()) {
        enricher.registerUploader(std::make_unique<Imgur>(imgurId));
    }

    EnrichedTrack lastTrack{};

    while (running) {
        auto [t, image] = applemusic.poll();
        if (t.identity != lastTrack.track.identity) {
            const EnrichedTrack enriched = enricher.enrich(t, image);
            log->info("track change: '{}' by '{}' ({})", enriched.track.identity.title,
                      enriched.track.identity.artist, enriched.track.identity.album);
            lastTrack = enriched;
        }

        discord.setPresence(lastTrack);

        SPDLOG_LOGGER_DEBUG(log, "poll: '{}' image={} ({})", lastTrack.track.identity.title,
                            lastTrack.image.url, lastTrack.image.source); {
            std::unique_lock lock(sleep_mutex);
            sleep_cv.wait_for(lock, std::chrono::seconds(5), [] {
                return !running.load();
            });
        }
    }

    return 0;
}
