//
// Created by Jonathan on 23-Nov-25.
//

#ifndef MUSICPP_LFM_H
#define MUSICPP_LFM_H

#include <chrono>
#include <string>

#include "utils.h"

namespace spdlog {
    class logger;
}

class lfm {
public:

    lfm(const std::string& apikey, const std::string& apisecret, spdlog::logger* logger = nullptr);

    ~lfm();

    lfm(const lfm &) = delete;

    lfm &operator=(const lfm &) = delete;

    // Does not require auth
    std::string searchTracks(const std::string &title, const std::string &artist) const;

    bool updateNowPlaying(const std::string &title, const std::string &artist, const std::string &album, const time_t& duration) const;

    bool scrobble(const std::string &title, const std::string &artist, const std::string &album, const time_t& start) const;

private:
    spdlog::logger *logger;

    std::string sessionKey;
    std::string apikey;
    std::string apisecret;

    std::time_t tokenValidity;

    bool enabled;

    std::string authRequestToken();

    bool authTestSession(const std::string &testKey) const;

    bool authGetSession(const std::string& token);

};


#endif //MUSICPP_LFM_H