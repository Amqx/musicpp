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
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <discord/rp.hpp>

[[noreturn]] int main() {
    auto applemusic = AmWin();
    const auto discord = RichPresence(1358389458956976128);
    MetadataCache cache;

    std::vector<std::unique_ptr<MetadataWebSource> > sources;
    sources.push_back(std::make_unique<Scraper>("ca"));

    std::vector<std::unique_ptr<Uploader> > uploaders;

    // Redacted
    if (std::string imgurId = ""; !imgurId.empty()) {
        uploaders.push_back(std::make_unique<Imgur>(imgurId));
    }

    Enricher enricher(cache, std::move(sources), std::move(uploaders));

    while (true) {
        auto [t, image] = applemusic.poll();
        const EnrichedTrack enriched = enricher.enrich(t, image);
        discord.setPresence(enriched);

        std::cout << enriched.track << std::endl;
        std::cout << "  image: " << enriched.image.url
            << " (" << enriched.image.source << ")" << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}
