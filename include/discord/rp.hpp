/**
 * @file rp.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#pragma once

#include <chrono>

#include "discordpp.h"
#include "types/track.hpp"

class RichPresence {
public:
    explicit RichPresence(const uint64_t &apikey);

    ~RichPresence();

    /**
     * Sets a new Discord rich presence.
     * @param track Enriched track details.
     */
    void setPresence(const EnrichedTrack &track) const;

private:
    std::unique_ptr<discordpp::Client> _client = std::make_unique<discordpp::Client>();
};
