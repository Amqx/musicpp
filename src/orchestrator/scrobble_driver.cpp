/**
 * @file scrobble_driver.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#include "orchestrator/scrobble_driver.hpp"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <utility>
#include <vector>

ScrobbleDriver::ScrobbleDriver(const ScrobbleSchedule &schedule) : _schedule(schedule) {
}

ScrobbleDriver::~ScrobbleDriver() {
    // Lets an attempt still sitting in the worker's queue bail out instead of making its call.
    _play.request_stop();
}

void ScrobbleDriver::registerScrobbler(std::shared_ptr<Scrobbler> scrobbler) {
    const auto now = std::chrono::steady_clock::now();
    _targets.push_back(Target{
        .scrobbler = std::move(scrobbler),
        .nowPlaying = {.nextAttempt = now},
        .scrobble = {.nextAttempt = now}
    });
}

ScrobbleDriver::Pending &ScrobbleDriver::slotFor(Target &target, const Attempt kind) {
    return kind == Attempt::NowPlaying ? target.nowPlaying : target.scrobble;
}

const char *ScrobbleDriver::name(const Attempt kind) {
    return kind == Attempt::NowPlaying ? "now-playing update" : "scrobble";
}

bool ScrobbleDriver::scrobbleThresholdMet(const Track &track) {
    const auto length = track.timing.total();
    if (length <= kScrobbleMinLength) {
        return false; // Too short to scrobble, per Last.fm.
    }
    const auto threshold = std::min<std::chrono::nanoseconds>(length / 2, kScrobblePlayCap);
    return track.timing.current() >= threshold;
}

void ScrobbleDriver::reset() {
    // Retires every attempt made for the play that just ended
    _play.request_stop();
    _play = std::stop_source{};

    const auto now = std::chrono::steady_clock::now();
    for (auto &target: _targets) {
        target.nowPlaying = Pending{.nextAttempt = now};
        target.scrobble = Pending{.nextAttempt = now};
    }
}

void ScrobbleDriver::tick(const Track &current) {
    drainResults();
    driveAttempts(current);
}

void ScrobbleDriver::drainResults() {
    std::vector<AttemptResult> results; {
        std::lock_guard lock{_resultsMutex};
        results.swap(_results);
    }
    for (const auto &result: results) {
        if (!result.play.stop_requested()) {
            applyResult(result);
            continue;
        }
        // The play is over, so the state this would have been applied to is already gone. The call
        // was still made, and one that landed is still worth a line.
        if (result.accepted) {
            _log->info("Accepted {} for '{}' by '{}' at {}, after the play ended",
                       name(result.kind), result.identity.title, result.identity.artist,
                       _targets[result.target].scrobbler->identify());
        }
    }
}

void ScrobbleDriver::applyResult(const AttemptResult &result) {
    auto &target = _targets[result.target];
    auto &pending = slotFor(target, result.kind);
    const auto &identity = result.identity;

    if (result.accepted) {
        pending.phase = Phase::Done;
        // An accepted now-playing update is armed to be re-sent should the track sit paused long
        // enough for it to go stale; nothing re-arms a scrobble.
        if (result.kind == Attempt::NowPlaying) {
            pending.nextAttempt = std::chrono::steady_clock::now() +
                                  _schedule.nowPlayingRefresh;
        }
        _log->info("Accepted {} for '{}' by '{}' at {}", name(result.kind), identity.title,
                   identity.artist, target.scrobbler->identify());
        return;
    }

    if (result.kind == Attempt::NowPlaying && pending.attempts >= _schedule.nowPlayingAttempts) {
        pending.phase = Phase::Done;
        // Given up on, so it is never re-sent, not even after a pause.
        pending.nextAttempt = std::chrono::steady_clock::time_point::max();
        _log->warn("Now-playing update for '{}' failed after {} attempts at {}", identity.title,
                   _schedule.nowPlayingAttempts, target.scrobbler->identify());
        return;
    }

    const auto retry = result.kind == Attempt::NowPlaying
                           ? _schedule.nowPlayingRetry
                           : _schedule.scrobbleRetry;
    pending.phase = Phase::Waiting;
    pending.nextAttempt = std::chrono::steady_clock::now() + retry;
    _log->debug("Rejected {} for '{}' at {}, retrying in {}ms", name(result.kind), identity.title,
                target.scrobbler->identify(), retry.count());
}

void ScrobbleDriver::driveAttempts(const Track &current) {
    const auto now = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < _targets.size(); ++index) {
        auto &target = _targets[index];
        if (!target.scrobbler->authed()) {
            continue;
        }
        for (const auto kind: {Attempt::NowPlaying, Attempt::Scrobble}) {
            auto &pending = slotFor(target, kind);

            // A now-playing update goes stale on the scrobbler's end after the refresh interval.
            // Re-send it once that has passed, but only while the track is actually playing: a
            // refresh falling due mid-pause is held until playback resumes.
            if (kind == Attempt::NowPlaying && pending.phase == Phase::Done &&
                current.status == Playing && now >= pending.nextAttempt) {
                pending.phase = Phase::Waiting;
                pending.attempts = 0;
            }

            if (pending.phase != Phase::Waiting || now < pending.nextAttempt) {
                continue;
            }
            // A scrobble waits on playback, not the clock: the track must have been played far
            // enough to warrant it.
            if (kind == Attempt::Scrobble && !scrobbleThresholdMet(current)) {
                continue;
            }
            // A now-playing update announces the track as being listened to right now, so it is held
            // while paused — the initial send just as much as the refresh guarded above. A play that
            // begins paused stays silent until it resumes.
            if (kind == Attempt::NowPlaying && current.status != Playing) {
                continue;
            }
            pending.phase = Phase::InFlight;
            ++pending.attempts;
            submitAttempt(index, kind, current);
        }
    }
}

void ScrobbleDriver::submitAttempt(const std::size_t target, const Attempt kind,
                                   const Track &track) {
    _worker.submit([this, target, kind, play = _play.get_token(), track,
        scrobbler = _targets[target].scrobbler] {
        if (play.stop_requested()) {
            return; // The play ended while this attempt sat in the queue.
        }
        const bool accepted =
                kind == Attempt::NowPlaying ? scrobbler->setPlaying(track) : scrobbler->scrobble(track);
        std::lock_guard lock{_resultsMutex};
        _results.push_back(AttemptResult{
            .play = play,
            .identity = track.identity,
            .target = target,
            .kind = kind,
            .accepted = accepted
        });
    });
}
