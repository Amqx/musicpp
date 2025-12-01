//
// Created by Jonathan on 26-Sep-25.
//

#ifndef MUSICPP_DISCORDRP_H
#define MUSICPP_DISCORDRP_H

#include <atomic>
#include <thread>
#include "discordpp.h"
#include "mediaPlayer.h"

namespace spdlog {
    class logger;
}

using namespace std;

class Discordrp {
public:
    Discordrp(MediaPlayer *player, const uint64_t &apikey, spdlog::logger *logger = nullptr);

    ~Discordrp();

    Discordrp(const Discordrp &) = delete;

    Discordrp &operator=(const Discordrp &) = delete;

    void update() const;

private:
    spdlog::logger *logger_;
    atomic<bool> running_{false};
    thread refresh_thread_;
    uint64_t client_id_;
    MediaPlayer *apple_music_;
    shared_ptr<discordpp::Client> client_ = std::make_shared<discordpp::Client>();

    void RefreshLoop() const;
};


#endif //MUSICPP_DISCORDRP_H