/**
 * @file orchestrator.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 28-Jun-26
 */

#pragma once

#include "metadata/enricher.hpp"
#include "metadata/scrobbler.hpp"

class Orchestrator {
public:
    Orchestrator();

    ~Orchestrator();

    void registerEnricher(std::unique_ptr<Enricher> enricher);

    /**
     * Registers a scrobbler the orchestrator drives during the poll loop.
     * @param scrobbler Shared handle to the scrobbler.
     */
    void registerScrobbler(std::shared_ptr<Scrobbler> scrobbler);

private:
    std::unique_ptr<Enricher> _enricher = nullptr;
    std::vector<std::shared_ptr<Scrobbler> > _scrobblers{};
};
