/**
 * @file orchestrator.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 28-Jun-26
 */

#pragma once

#include "discord/rp.hpp"
#include "metadata/enricher.hpp"
#include "metadata/scrobbler.hpp"
#include "system/poller.hpp"

class Orchestrator {
public:
    Orchestrator();

    ~Orchestrator();

    /**
     * Registers an enricher the orchestrator drives during the poll loop.
     * @param enricher Unique ptr to the scrobbler.
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
    std::unique_ptr<Enricher> _enricher = nullptr;
    std::vector<std::shared_ptr<Scrobbler> > _scrobblers{};
    std::unique_ptr<Poller> _poller = nullptr;
    std::unique_ptr<RichPresence> _discord = nullptr;

    EnrichedTrack prevTrack;
};
