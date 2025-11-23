//
// Created by Jonathan on 26-Sep-25.
//

#ifndef MUSICPP_DISCORDRP_H
#define MUSICPP_DISCORDRP_H

#include <discordpp.h>
#include <mediaPlayer.h>
#include <atomic>
#include <thread>
#include <spdlog/spdlog.h>

using namespace std;

class discordrp {
public:
    discordrp(mediaPlayer *player, const uint64_t& apikey, spdlog::logger* logger = nullptr);

    ~discordrp();

    discordrp(const discordrp &) = delete;

    discordrp &operator=(const discordrp &) = delete;

    void update() const;

private:
    spdlog::logger* logger;
    atomic<bool> running{false};
    thread refreshThread;
    uint64_t clientID;
    mediaPlayer *appleMusic;
    shared_ptr<discordpp::Client> client = std::make_shared<discordpp::Client>();

    void refreshLoop() const;
};


#endif //MUSICPP_DISCORDRP_H
