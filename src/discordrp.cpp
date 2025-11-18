//
// Created by Jonathan on 26-Sep-25.
//

#include <discordrp.h>
#include <stringutils.h>


discordrp::discordrp(mediaPlayer *player, const uint64_t apikey, spdlog::logger* logger) {
    this->appleMusic = player;
    this->clientID = apikey;
    running = true;
    this->refreshThread = thread(&discordrp::refreshLoop, this);
    if (logger) {
        logger->debug("Refresh thread started for discordrp.");
    }
    this -> logger = logger;
    client->SetApplicationId(clientID);
    client->Connect();
    if (logger) {
        logger->info("discordrp connection initialized.");
    }
}

discordrp::~discordrp() {
    running = false;
    client->Disconnect();
    if (refreshThread.joinable()) refreshThread.join();
    if (this -> logger) {
        logger -> info("discordrp Killed");
    }
}

void discordrp::update() const {
    if (!appleMusic->getTitle().empty() && !appleMusic->getArtist().empty() && !appleMusic->getAlbum().empty()) {
        discordpp::Activity activity;
        activity.SetType(discordpp::ActivityTypes::Listening);
        activity.SetName(convertWString(appleMusic->getArtist()));
        activity.SetDetails(convertWString(appleMusic->getTitle())); // title
        activity.SetState(convertWString(appleMusic->getArtist())); // artist

        // enable listening to:
        activity.SetStatusDisplayType(discordpp::StatusDisplayTypes::State);

        if (appleMusic->getState()) {
            discordpp::ActivityTimestamps timestamps;
            timestamps.SetStart(appleMusic->getStartTS());
            timestamps.SetEnd(static_cast<uint64_t>(appleMusic->getEndTS()));
            activity.SetTimestamps(timestamps);

            discordpp::ActivityAssets assets;
            assets.SetLargeImage(convertWString(appleMusic->getImage()));
            assets.SetLargeText(convertWString(appleMusic->getAlbum()));
            activity.SetAssets(assets);
        } else {
            discordpp::ActivityTimestamps timestamps;
            timestamps.SetStart(appleMusic->getPauseTimer());
            activity.SetTimestamps(timestamps);

            discordpp::ActivityAssets assets;
            assets.SetLargeImage(convertWString(appleMusic->getImage()));
            assets.SetLargeText(convertWString(appleMusic->getAlbum()));
            assets.SetSmallImage("pause");
            assets.SetSmallText("Paused");
            activity.SetAssets(assets);
        }
        if (logger) {
            logger->debug("Updating discordrp presence");
        }
        client->UpdateRichPresence(activity, [logger = this ->logger](const discordpp::ClientResult &result) {
            if (!result.Successful()) {
                if (logger) {
                    logger -> error("Discord RPC update failed ({}): {}", static_cast<int>(result.Type()), result.Error());
                }
            }
        });
    } else {
        if (logger) {
            logger->debug("Clearing Discord Rich Presence (missing metadata).");
        }
        client->ClearRichPresence();
    }
}

void discordrp::refreshLoop() const {
    while (running) {
        try {
            discordpp::RunCallbacks();
        } catch (exception &e) {
            if (logger) {
                logger -> warn("Error in discordrp refresh loop: {}", e.what());
            }
        }
        this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}