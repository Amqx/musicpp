//
// Created by Jonathan on 23-Nov-25.
//

#ifndef MUSICPP_LFM_H
#define MUSICPP_LFM_H

#include <string>
#include "utils.h"

namespace spdlog {
    class logger;
}

class Lfm {
public:
    Lfm(const std::string &apikey, const std::string &apisecret, spdlog::logger *logger = nullptr);

    ~Lfm();

    Lfm(const Lfm &) = delete;

    Lfm &operator=(const Lfm &) = delete;

    // Does not require auth
    std::string SearchTracks(const std::string &title, const std::string &artist) const;

    bool UpdateNowPlaying(const std::string &title, const std::string &artist, const std::string &album,
                          const uint64_t &duration) const;

    bool scrobble(const std::string &title, const std::string &artist, const std::string &album,
                  const uint64_t &start) const;

private:
    spdlog::logger *logger_;

    std::string session_key_;
    std::string apikey_;
    std::string apisecret_;

    uint64_t token_validity_;

    bool enabled_;

    std::string AuthRequestToken();

    bool AuthTestSession(const std::string &test_key) const;

    bool AuthGetSession(const std::string &token);
};


#endif //MUSICPP_LFM_H