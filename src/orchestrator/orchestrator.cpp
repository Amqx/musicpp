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
    _scrobblers.push_back(std::move(scrobbler));
}
