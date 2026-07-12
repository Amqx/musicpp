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
#include <memory>
#include <thread>
#include <discord/rp.hpp>

[[noreturn]] int main() {
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

    TrackIdentity lastIdentity;

    while (true) {
        auto [t, image] = applemusic.poll();
        const EnrichedTrack enriched = enricher.enrich(t, image);
        discord.setPresence(enriched);

        if (enriched.track.identity != lastIdentity) {
            log->info("track change: '{}' by '{}' ({})", enriched.track.identity.title,
                      enriched.track.identity.artist, enriched.track.identity.album);
            lastIdentity = enriched.track.identity;
        }

        SPDLOG_LOGGER_DEBUG(log, "poll: '{}' image={} ({})", enriched.track.identity.title,
                            enriched.image.url, enriched.image.source);

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
