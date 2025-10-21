//
// Created by Jonathan on 26-Sep-25.
//

#ifndef MUSICPP_DISCORDRP_H
#define MUSICPP_DISCORDRP_H

#include <string>
#include <discordsdk/include/discordpp.h>
#include <include/mediaPlayer.h>
#include <atomic>
#include <thread>

using namespace std;

class discordrp {
public:
    discordrp(mediaPlayer *player, const uint64_t apikey);

    ~discordrp();

    discordrp(const discordrp &) = delete;

    discordrp &operator=(const discordrp &) = delete;

    void update();

private:
    atomic<bool> running{false};
    thread refreshThread;
    uint64_t clientID;
    mediaPlayer *appleMusic;
    shared_ptr<discordpp::Client> client = std::make_shared<discordpp::Client>();

    void refreshLoop();

    string convertWString(const wstring &wstr);
};


#endif //MUSICPP_DISCORDRP_H