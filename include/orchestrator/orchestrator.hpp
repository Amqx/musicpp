/**
 * @file orchestrator.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 28-Jun-26
 */

#pragma once

#include <chrono>

#include "discord/rp.hpp"
#include "log/log.hpp"
#include "metadata/enricher.hpp"
#include "metadata/scrobbler.hpp"
#include "system/poller.hpp"

/// Wall time after a new play begins before the first scrobble is attempted.
constexpr std::chrono::seconds kScrobbleDelay{30};

/// Wall time between scrobble attempts after one was rejected.
constexpr std::chrono::seconds kScrobbleRetry{30};

/// A backwards jump in playback position larger than this counts as a new play of the same track.
constexpr std::chrono::seconds kRestartThreshold{30};

class Orchestrator {
public:
    Orchestrator();

    ~Orchestrator();

    /**
     * Registers an enricher the orchestrator drives during the poll loop.
     * @param enricher Unique ptr to the enricher.
     */
    void registerEnricher(std::unique_ptr<Enricher> enricher);

    /**
     * Registers a scrobbler the orchestrator drives during the poll loop.
     * @param scrobbler Shared handle to the scrobbler.
     */
    void registerScrobbler(std::shared_ptr<Scrobbler> scrobbler);

    /**
     * Registers a poller the orchestrator drives during the poll loop.
     * @param poller Unique ptr to the poller.
     */
    void registerPoller(std::unique_ptr<Poller> poller);

    /**
     * Registers a rich presence client the orchestrator drives during the poll loop.
     * @param discord Unique ptr to the Rich Presence client.
     */
    void registerRichPresence(std::unique_ptr<RichPresence> discord);

    /**
     * Performs a cycle of the orchestrator.
     */
    void run();

private:
    /**
     * A registered scrobbler plus the per-play state deciding when it is next called.
     */
    struct ScrobbleTarget {
        std::shared_ptr<Scrobbler> scrobbler;
        bool scrobbled = false;
        std::chrono::steady_clock::time_point nextAttempt{};
    };

    /**
     * Whether a polled track is the current one started over.
     * @param track Track polled this cycle.
     * @return Whether the track is a fresh play of the track already being presented.
     */
    [[nodiscard]] bool isRestart(const Track &track) const;

    /**
     * Resets the per-play state carried across cycles. Called whenever a new play begins, whether
     * that is a different track or the current one started over.
     */
    void resetPlayState();

    /**
     * Drops the current track and tears the presence down. Called when nothing is playing at all,
     * which the poller reports as an empty track.
     */
    void clearTrack();

    /**
     * Records the instant a pause began, and clears it once playback resumes.
     */
    void updatePauseDetails();

    /**
     * Attempts a scrobble for every scrobbler that is due one. A scrobbler that accepted the play
     * is not called again until the next one begins.
     */
    void driveScrobblers();

    std::shared_ptr<spdlog::logger> _log = logging::get("orchestrator");

    std::unique_ptr<Enricher> _enricher = nullptr;
    std::vector<ScrobbleTarget> _scrobblers{};
    std::unique_ptr<Poller> _poller = nullptr;
    std::unique_ptr<RichPresence> _discord = nullptr;

    EnrichedTrack _current{};

    /// Playback position observed on the previous cycle, used to spot a restart.
    std::chrono::nanoseconds _position{};
};
