/**
 * @file poller.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#pragma once
#include "types/track.hpp"
#include <string>
#include <optional>

class Poller {
public:
    virtual ~Poller() = default;

    /**
     * Polls the system for the current track and thumbnail.
     * @return Tuple containing track info and an optional raw thumbnail.
     */
    virtual std::tuple<Track, std::optional<std::vector<unsigned char>>> poll() = 0;

    /**
     * Identifies the player.
     * @return Player identity string.
     */
    virtual std::string identify() = 0;
};
