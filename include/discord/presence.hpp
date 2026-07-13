/**
 * @file presence.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#pragma once

#include "types/track.hpp"

/**
 * Capability for clients that can publish what is playing as a user-visible presence.
 */
class Presence {
public:
    virtual ~Presence() = default;

    /**
     * Publishes a new presence.
     * @param track Enriched track details, carrying its own pause details.
     */
    virtual void setPresence(const EnrichedTrack &track) const = 0;

    /**
     * Tears the presence down, leaving the user with no activity. Used when nothing is playing at
     * all, which is distinct from a paused or stopped track.
     */
    virtual void clearPresence() const = 0;
};
