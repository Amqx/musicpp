/**
 * @file rp.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#include "discord/rp.hpp"
#include "discord/refresh.hpp"
#include "log/log.hpp"

/**
 * Default runtime log level from build configuration.
 * @return Log level.
 */
constexpr discordpp::LoggingSeverity defaultLevel() {
#ifdef NDEBUG
    return discordpp::LoggingSeverity::Info;
#else
    return discordpp::LoggingSeverity::Verbose;
#endif
}

RichPresence::RichPresence(const uint64_t &apikey) {
    DiscordRefresher::initialize();
    _client->AddLogCallback([](auto message, const auto &severity) {
        const auto &logger = logging::get("discord");
        while (!message.empty() && (message.back() == '\0' || message.back() == '\n' ||
                                    message.back() == '\r')) {
            message.pop_back();
        }
        switch (severity) {
        case discordpp::LoggingSeverity::Info: {
            logger->debug(message);
            break;
        }
        case discordpp::LoggingSeverity::Error: {
            logger->error(message);
            break;
        }
        case discordpp::LoggingSeverity::Warning: {
            logger->warn(message);
            break;
        }
        case discordpp::LoggingSeverity::None:
        case discordpp::LoggingSeverity::Verbose:
        default: {
            logger->trace(message);
            break;
        }
        }
    }, defaultLevel());
    _client->AddVoiceLogCallback([](auto message, const auto &severity) {
        const auto &logger = logging::get("discord");
        while (!message.empty() && (message.back() == '\0' || message.back() == '\n' ||
                                    message.back() == '\r')) {
            message.pop_back();
        }
        switch (severity) {
        case discordpp::LoggingSeverity::Error: {
            logger->error(message);
            break;
        }
        case discordpp::LoggingSeverity::Warning: {
            logger->warn(message);
            break;
        }
        case discordpp::LoggingSeverity::Info:
        case discordpp::LoggingSeverity::None:
        case discordpp::LoggingSeverity::Verbose:
        default: {
            logger->trace(message);
            break;
        }
        }
    }, defaultLevel());
    _client->SetStatusChangedCallback(
        [](const auto &status, const auto &error, const auto &errorDetails) {
            const auto &logger = logging::get("discord");
            auto message = "Status changed: " + discordpp::Client::StatusToString(status);
            if (error != discordpp::Client::Error::None) {
                auto m = discordpp::Client::ErrorToString(error);
                while (!m.empty() && m.back() == '\0' || m.back() == '\r' || m.back() == '\n') {
                    m.pop_back();
                }
                message += "(" + m + ", " + std::to_string(errorDetails) + ")";
                logger->error(message);
            } else {
                logger->info(message);
            }
        });
    _client->SetApplicationId(apikey);
    _client->Connect();
}

/**
 * Makes sure strings passed to the Discord Social SDK are between 2 and 128 characters.
 * @param str Intended input string.
 * @return Sanitized string.
 */
std::string discordStringBounds(const std::string &str) {
    constexpr int kDiscordMaxStrLen = 128;
    const auto kWhitespace = reinterpret_cast<const char *>(u8"\u2008");
    const auto len = str.length();
    if (len > kDiscordMaxStrLen) {
        auto l = str.begin();
        while (l != str.end() && isspace(*l))
            ++l;
        auto r = str.end();
        do {
            if (r == l) {
                break;
            }
            --r;
        } while (isspace(*r));

        std::string out;
        if (r >= l) {
            out.assign(l, r + 1);
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
void setIdentity(discordpp::Activity &activity, const Track &track) {
    activity.SetName(discordStringBounds(track.identity.artist));
    activity.SetDetails(discordStringBounds(track.identity.title));
    activity.SetState(discordStringBounds(track.identity.artist));
    activity.SetStatusDisplayType(discordpp::StatusDisplayTypes::Details);
    activity.SetType(discordpp::ActivityTypes::Listening);
}

void setTimeline(discordpp::Activity &activity, const Track &track) {
    if (track.status == Playing) {
        discordpp::ActivityTimestamps timestamps;
        timestamps.SetStart(track.timing.start());
        timestamps.SetEnd(track.timing.end());
        activity.SetTimestamps(timestamps);
    }
}

/**
 * Sets an activity's large image (album art) and a click-through url from the
 * enriched track. No-op when no image was found.
 * @param activity Discord activity object.
 * @param track Enriched track details.
 */
void setAssets(discordpp::Activity &activity, const EnrichedTrack &track) {
    if (track.image.url.empty()) {
        return;
    }
    discordpp::ActivityAssets assets;
    assets.SetLargeImage(track.image.url);
    if (!track.track.identity.album.empty()) {
        assets.SetLargeText(discordStringBounds(track.track.identity.album));
    }
    if (!track.songUrls.empty() && track.songUrls.front().url.length() <= 256) {
        assets.SetLargeUrl(track.songUrls.front().url);
    }
    activity.SetAssets(assets);
}

void setButtons(discordpp::Activity &activity, const EnrichedTrack &track) {
    if (track.songUrls.empty()) {
        return;
    }

    int count = 0;
    for (const auto &[url, source] : track.songUrls) {
        if (count == 2)
            break;
        discordpp::ActivityButton button;

        if (source == "Apple Music Web Scraper") {
            button.SetLabel("Apple Music");
        } else {
            button.SetLabel(source);
        }

        button.SetUrl(url);
        activity.AddButton(button);
        count++;
    }
}

void RichPresence::setPresence(const EnrichedTrack &track) const {
    discordpp::Activity activity;
    setIdentity(activity, track.track);
    setTimeline(activity, track.track);
    setAssets(activity, track);
    setButtons(activity, track);

    _client->UpdateRichPresence(activity, [](const discordpp::ClientResult &result) {
        (void)result;
    });
}

RichPresence::~RichPresence() {
    _client->Disconnect();
}
