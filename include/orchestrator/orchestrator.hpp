/**
 * @file orchestrator.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 28-Jun-26
 */

#pragma once

#include <chrono>
#include <memory>

#include "discord/rp.hpp"
#include "log/log.hpp"
#include "metadata/enricher.hpp"
#include "metadata/scrobbler.hpp"
#include "orchestrator/scrobble_driver.hpp"
#include "system/poller.hpp"

/// A backwards jump in playback position larger than this counts as a new play of the same track.
constexpr std::chrono::seconds kRestartThreshold{30};

class Orchestrator {
public:
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
     * Whether a polled track is the current one started over.
     * @param track Track polled this cycle.
     * @return Whether the track is a fresh play of the track already being presented.
     */
    [[nodiscard]] bool isRestart(const Track &track) const;

    /**
     * Drops the current track and tears the presence down. Called when nothing is playing at all,
     * which the poller reports as an empty track.
     */
    void clearTrack();

    /**
     * Records the instant a pause began, and clears it once playback resumes.
     */
    void updatePauseDetails();

    std::shared_ptr<spdlog::logger> _log = logging::get("orchestrator");

    std::unique_ptr<Enricher> _enricher = nullptr;
    std::unique_ptr<Poller> _poller = nullptr;
    std::unique_ptr<RichPresence> _discord = nullptr;

    /// Drives the registered scrobblers through the play being presented.
    ScrobbleDriver _scrobbles{};

    EnrichedTrack _current{};

    /// Playback position observed on the previous cycle, used to spot a restart.
    std::chrono::nanoseconds _position{};
};
