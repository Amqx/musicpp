/**
 * @file rp.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#include "discord/rp.hpp"
#include "discord/refresh.hpp"

RichPresence::RichPresence(const uint64_t &apikey) {
    DiscordRefresher::initialize();
    _client -> SetApplicationId(apikey);
    _client -> Connect();
}

/**
 * Makes sure strings passed to the Discord Social SDK are between 2 and 128 characters.
 * @param str Intended input string.
 * @return Sanitized string.
 */
std::string discordStringBounds(const std::string& str) {
    constexpr int kDiscordMaxStrLen = 128;
    const auto kWhitespace = reinterpret_cast<const char*>(u8"\u2008");
    const auto len = str.length();
    if (len > kDiscordMaxStrLen) {
        auto l = str.begin();
        while (l != str.end() && isspace(*l)) ++l;
        auto r = str.end();
        do {
            if (r == l) {
                break;
            }
            --r;
        } while (isspace(*r));

        std::string out;
        if (r >= l) {
            out.assign(l, r+1);
        }
        if (out.size() > kDiscordMaxStrLen) {
            out.resize(kDiscordMaxStrLen);
        }
        return out;
    }

    if (len < 2) {
        std::string out = str;
        do {
            out += kWhitespace;
        } while (out.length() < 2);
        return out;
    }
    return str;
}

/**
 * Sets an activity's details according to a track's identity.
 * @param activity Discord activity object.
 * @param track Track details.
 */
void setIdentity(discordpp::Activity& activity, const Track& track) {
    activity.SetName(discordStringBounds(track.identity.artist));
    activity.SetDetails(discordStringBounds(track.identity.title));
    activity.SetState(discordStringBounds(track.identity.artist));
    activity.SetStatusDisplayType(discordpp::StatusDisplayTypes::Details);
    activity.SetType(discordpp::ActivityTypes::Listening);
}

void setTimeline(discordpp::Activity& activity, const Track& track) {
    if (track.status == Playing) {
        discordpp::ActivityTimestamps timestamps;
        timestamps.SetStart(track.timing.start());
        timestamps.SetEnd(track.timing.end());
        activity.SetTimestamps(timestamps);
    }
}

void RichPresence::setPresence(const Track &track) const {
    discordpp::Activity activity;
    setIdentity(activity, track);
    setTimeline(activity, track);
    _client->UpdateRichPresence(activity, [] (const discordpp::ClientResult &result) {
        (void)result;
    });
}

RichPresence::~RichPresence() {
    _client -> Disconnect();
}
