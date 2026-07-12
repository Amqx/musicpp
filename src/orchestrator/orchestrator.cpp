/**
 * @file orchestrator.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 28-Jun-26
 */

#include "orchestrator/orchestrator.hpp"

#include "log/log.hpp"
#include "metadata/scrobbler.hpp"

Orchestrator::Orchestrator() = default;

Orchestrator::~Orchestrator() = default;

void Orchestrator::registerEnricher(std::unique_ptr<Enricher> enricher) {
    _enricher = std::move(enricher);
}

void Orchestrator::registerScrobbler(std::shared_ptr<Scrobbler> scrobbler) {
    _scrobblers.push_back(std::move(scrobbler));
}

void Orchestrator::registerPoller(std::unique_ptr<Poller> poller) {
    _poller = std::move(poller);
}

void Orchestrator::registerRichPresence(std::unique_ptr<RichPresence> discord) {
    _discord = std::move(discord);
}

void Orchestrator::run() {
    const auto log = logging::get("orchestrator");
    if (!_poller)
        return;
    auto [track, image] = _poller->poll();
    if (track.identity != prevTrack.track.identity) {
        EnrichedTrack enriched = {.track = track};
        if (_enricher)
            enriched = _enricher->enrich(track, image);
        log->info("track change: '{}' by '{}' ([])", enriched.track.identity.title,
                  enriched.track.identity.artist, enriched.track.identity.album);
        prevTrack = enriched;
    }
    if (_discord)
        _discord->setPresence(prevTrack);
}
