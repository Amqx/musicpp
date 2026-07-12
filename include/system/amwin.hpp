/**
 * @file amwin.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#pragma once
#include "system/poller.hpp"

class AmWin : public Poller {
public:
    AmWin();

    std::string identify() override;

    std::tuple<Track, std::optional<std::vector<unsigned char> > > poll() override;

private:
    const std::string kIdentifier = "Apple Music, Windows";
};
