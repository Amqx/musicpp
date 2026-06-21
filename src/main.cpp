/**
 * @file main.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#include "types/track.hpp"
#include "system/amwin.hpp"
#include <iostream>
#include <thread>
#include<discord/rp.hpp>

int main() {
    auto applemusic = AmWin();
    const auto discord = RichPresence(1358389458956976128);
    while (true) {
        auto [t, image] = applemusic.poll();
        discord.setPresence(t);
        std::cout << t << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    return 0;
}
