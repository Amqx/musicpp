/**
 * @file orchestrator.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 28-Jun-26
 */

#include "orchestrator/orchestrator.hpp"

#include "metadata/scrobbler.hpp"

Orchestrator::Orchestrator() = default;

Orchestrator::~Orchestrator() = default;

void Orchestrator::registerEnricher(std::unique_ptr<Enricher> enricher) {
    _enricher = std::move(enricher);
}

void Orchestrator::registerScrobbler(std::shared_ptr<Scrobbler> scrobbler) {
    _scrobblers.push_back(ScrobbleTarget{
        .scrobbler = std::move(scrobbler),
        .nextAttempt = std::chrono::steady_clock::now() + kScrobbleDelay
    });
}

void Orchestrator::registerPoller(std::unique_ptr<Poller> poller) {
    _poller = std::move(poller);
}

void Orchestrator::registerRichPresence(std::unique_ptr<RichPresence> discord) {
    _discord = std::move(discord);
}

bool Orchestrator::isRestart(const Track &track) const {
    return track.timing.current() + kRestartThreshold < _position;
}

void Orchestrator::resetPlayState() {
    _current.pause.since.reset();
    const auto firstAttempt = std::chrono::steady_clock::now() + kScrobbleDelay;
    for (auto &target : _scrobblers) {
        target.scrobbled = false;
        target.nextAttempt = firstAttempt;
    }
}

void Orchestrator::clearTrack() {
    if (_current.track.identity.title.empty()) {
        return; // Nothing was being presented, so there is nothing to tear down.
    }
    _log->info("Playback ended");
    _current = {};
    _position = std::chrono::nanoseconds::zero();
    resetPlayState();
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

void Orchestrator::driveScrobblers() {
    const auto &track = _current.track;
    const auto now = std::chrono::steady_clock::now();
    for (auto &[scrobbler, scrobbled, nextAttempt] : _scrobblers) {
        if (scrobbled || now < nextAttempt || !scrobbler->authed()) {
            continue;
        }
        if (scrobbler->scrobble(track)) {
            scrobbled = true;
            _log->info("Scrobbled '{}' by '{}'", track.identity.title, track.identity.artist);
        } else {
            // Also the path taken while the track is simply not played far enough to scrobble yet.
            nextAttempt = now + kScrobbleRetry;
            _log->debug("Scrobble of '{}' was not accepted, retrying in {}s",
                        track.identity.title, kScrobbleRetry.count());
        }
    }
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
        resetPlayState();
        _log->info("Track change: '{}' by '{}' ({})", _current.track.identity.title,
                   _current.track.identity.artist, _current.track.identity.album);
    } else {
        if (isRestart(track)) {
            // The same track played again is a new play: it is scrobbled again, but the
            // enrichment already paid for still describes it.
            resetPlayState();
            _log->info("Track restarted: '{}' by '{}'", _current.track.identity.title,
                       _current.track.identity.artist);
        }
        // Same track, so keep the enrichment already paid for and take the fresh timing and status.
        _current.track = track;
    }
    _position = _current.track.timing.current();

    updatePauseDetails();
    driveScrobblers();

    if (_discord) {
        _discord->setPresence(_current);
    }
}
