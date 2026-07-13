/**
 * @file scrobbler.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 28-Jun-26
 */

#pragma once

#include "types/track.hpp"

/**
 * Capability for services that can scrobble played tracks to a user's account.
 */
class Scrobbler {
public:
    virtual ~Scrobbler() = default;

    /**
     * @return Whether a valid user session is currently available.
     */
    [[nodiscard]] virtual bool authed() const = 0;

    /**
     * Drives the user-authentication flow.
     * @return Whether authentication succeeded.
     */
    [[nodiscard]] virtual bool authenticateUser() = 0;

    /**
     * Scrobbles a track to the authenticated user's account.
     * @param track Track to scrobble.
     * @return Whether the scrobble was accepted.
     */
    [[nodiscard]] virtual bool scrobble(const Track &track) const = 0;

    /**
     * Sets the user's currently playing status.
     * @param track Track currently playing.
     * @return Whether the status set was accepted.
     */
    [[nodiscard]] virtual bool setPlaying(const Track &track) const = 0;

    [[nodiscard]] virtual std::string identify() = 0;
};