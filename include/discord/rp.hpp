/**
 * @file rp.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#pragma once

#include <chrono>

#include "discord/presence.hpp"
#include "discordpp.h"
#include "types/track.hpp"

class RichPresence final : public Presence {
public:
    explicit RichPresence(const uint64_t &apikey);

    ~RichPresence() override;

    /**
     * Sets a new Discord rich presence.
     * @param track Enriched track details, carrying its own pause details.
     */
    void setPresence(const EnrichedTrack &track) const override;

    /**
     * Tears the rich presence down, leaving the user with no activity. Used when nothing is
     * playing at all, which is distinct from a paused or stopped track.
     */
    void clearPresence() const override;

private:
    std::shared_ptr<discordpp::Client> _client = std::make_shared<discordpp::Client>();
};
