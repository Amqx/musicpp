/**
 * @file orchestrator.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 28-Jun-26
 */

#include "orchestrator/orchestrator.hpp"
#include "orchestrator/scrobble_driver.hpp"

#include <chrono>
#include <utility>

void Orchestrator::registerEnricher(std::unique_ptr<Enricher> enricher) {
    _enricher = std::move(enricher);
}

void Orchestrator::registerScrobbler(std::shared_ptr<Scrobbler> scrobbler) {
    _scrobbles.registerScrobbler(std::move(scrobbler));
}

void Orchestrator::registerPoller(std::unique_ptr<Poller> poller) {
    _poller = std::move(poller);
}

void Orchestrator::registerRichPresence(std::unique_ptr<Presence> discord) {
    _discord = std::move(discord);
}

bool Orchestrator::isRestart(const Track &track) const {
    return track.timing.current() + kRestartThreshold < _position;
}

void Orchestrator::clearTrack() {
    if (_current.track.identity.title.empty()) {
        return; // Nothing was being presented, so there is nothing to tear down.
    }
    _log->info("Playback ended");
    _current = {};
    _position = std::chrono::nanoseconds::zero();
    _scrobbles.reset();
    if (_discord) {
        _discord->clearPresence();
    }
}

void Orchestrator::updatePauseDetails() {
    if (_current.track.status != Paused) {
        if (_current.pause.since.has_value()) {
            _log->debug("Playback resumed");
        }
        _current.pause.since.reset();
        return;
    }
    if (_current.pause.since.has_value()) {
        return;
    }
    _current.pause.since = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    _log->debug("Playback paused");
}

void Orchestrator::run() {
    if (!_poller) {
        return;
    }

    auto [track, image] = _poller->poll();

    // An empty identity is the poller's way of saying no player is running.
    if (track.identity.title.empty()) {
        clearTrack();
        return;
    }

    if (track.identity != _current.track.identity) {
        _current = _enricher ? _enricher->enrich(track, image) : EnrichedTrack{.track = track};
        _current.pause.since.reset();
        _scrobbles.reset();
        _log->info("Track change: '{}' by '{}' ({})", _current.track.identity.title,
                   _current.track.identity.artist, _current.track.identity.album);
    } else {
        if (isRestart(track)) {
            // The same track played again is a new play: it is scrobbled again, but the
            // enrichment already paid for still describes it.
            _current.pause.since.reset();
            _scrobbles.reset();
            _log->info("Track restarted: '{}' by '{}'", _current.track.identity.title,
                       _current.track.identity.artist);
        }
        // Same track, so keep the enrichment already paid for and take the fresh timing and status.
        _current.track = track;
    }
    _position = _current.track.timing.current();

    updatePauseDetails();
    _scrobbles.tick(_current.track);

    if (_discord) {
        _discord->setPresence(_current);
    }
}