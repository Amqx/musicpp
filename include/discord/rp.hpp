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
     * @param track Enriched track details, carrying its own pause details.
     */
    void setPresence(const EnrichedTrack &track) const;

    /**
     * Tears the rich presence down, leaving the user with no activity. Used when nothing is
     * playing at all, which is distinct from a paused or stopped track.
     */
    void clearPresence() const;

private:
    std::shared_ptr<discordpp::Client> _client = std::make_shared<discordpp::Client>();
};
