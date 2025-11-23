//
// Created by Jonathan on 19-Nov-25.
//

#ifndef MUSICPP_AMSCRAPER_H
#define MUSICPP_AMSCRAPER_H

#include <string>
#include <utils.h>

namespace spdlog {
    class logger;
}

class amscraper {
public:
    explicit amscraper(const std::string &region, spdlog::logger *logger = nullptr);

    ~amscraper();

    searchResult searchTracks(const std::string &title, const std::string &artist, const std::string &album) const;

private:
    std::string region;
    spdlog::logger *logger;
};


#endif //MUSICPP_AMSCRAPER_H