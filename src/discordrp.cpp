//
// Created by Jonathan on 26-Sep-25.
//

#include <discordrp.h>
#include <stringutils.h>


discordrp::discordrp(mediaPlayer *player, const uint64_t &apikey, spdlog::logger *logger) {
    this->appleMusic = player;
    this->clientID = apikey;
    running = true;
    this->refreshThread = thread(&discordrp::refreshLoop, this);
    if (logger) {
        logger->debug("Refresh thread started for discordrp");
    }
    this->logger = logger;
    client->SetApplicationId(clientID);
    client->Connect();
    if (logger) {
        logger->info("discordrp connection initialized");
    }
}

discordrp::~discordrp() {
    running = false;
    client->Disconnect();
    if (refreshThread.joinable()) refreshThread.join();
    if (this->logger) {
        logger->info("discordrp Killed");
    }
}

void discordrp::update() const {
    if (!appleMusic->getTitle().empty() && !appleMusic->getArtist().empty() && !appleMusic->getAlbum().empty()) {
        discordpp::Activity activity;

        const string title = discord_bounds(appleMusic->getTitle(), "Unknown Song");
        const string artist = discord_bounds(appleMusic->getArtist(), "Unknown Artist");
        const string album = discord_bounds(appleMusic->getAlbum(), "Unknown Album");
        const string imglink = convertWString(appleMusic->getImage());
        const string amlink = convertWString(appleMusic->getAMLink());
        const string LFMlink = convertWString(appleMusic->getLastFMLink());
        const string splink = convertWString(appleMusic->getSpotifyLink());
        const bool playing = appleMusic->getState();
        const uint64_t start_ts = appleMusic->getStartTS();
        const uint64_t end_ts = appleMusic->getEndTS();
        const uint64_t pause_ts = appleMusic->getPauseTimer();

        // Basic info
        activity.SetName(artist);
        activity.SetDetails(title);
        activity.SetState(artist);
        activity.SetStatusDisplayType(discordpp::StatusDisplayTypes::Details);
        activity.SetType(discordpp::ActivityTypes::Listening);
        if (logger) {
            logger->debug("Activity (name, details, state): '{}' '{}' '{}'", artist + convertWString(L" \uf3b4"), title,
                          artist);
        }

        // Images
        discordpp::ActivityAssets assets;
        assets.SetLargeImage(imglink);
        if (imglink != "default") {
            assets.SetLargeUrl(imglink);
        }
        assets.SetLargeText(album);
        if (logger) {
            logger->debug("Assets (large image, large text): '{}' '{}'", imglink, album);
        }

        // Time
        discordpp::ActivityTimestamps timestamps;
        if (playing) {
            timestamps.SetStart(start_ts);
            timestamps.SetEnd(end_ts);
            activity.SetTimestamps(timestamps);
            if (logger) {
                logger->debug("Timestamps (start, end): '{}' '{}'", appleMusic->getStartTS(), appleMusic->getEndTS());
            }
        } else {
            timestamps.SetStart(pause_ts);
            activity.SetTimestamps(timestamps);
            assets.SetSmallImage("pause");
            assets.SetSmallText("Paused");
            if (logger) {
                logger->debug("Timestamps (paused at): '{}'", pause_ts);
            }
        }

        // Buttons
        if (!amlink.empty()) {
            discordpp::ActivityButton button1;
            button1.SetLabel("Apple Music");
            button1.SetUrl(amlink);
            activity.AddButton(button1);
            if (logger) {
                if (button1) {
                    logger->debug("Added Apple Music button with URL: '{}'", amlink);
                } else {
                    logger->debug("Invalid Apple Music button with URL: '{}'", amlink);
                }
            }
        }
        if (!LFMlink.empty() || !splink.empty()) {
            discordpp::ActivityButton button2;
            if (!LFMlink.empty()) {
                button2.SetLabel("LastFM");
                button2.SetUrl(LFMlink);
                activity.AddButton(button2);
                if (logger) {
                    if (button2) {
                        logger->debug("Added LastFM button with URL: '{}'", LFMlink);
                    } else {
                        logger->debug("Invalid LastFM button with URL: '{}'", LFMlink);
                    }
                }
            } else {
                button2.SetLabel("Spotify");
                button2.SetUrl(splink);
                activity.AddButton(button2);
                if (logger) {
                    if (button2) {
                        logger->debug("Added Spotify button with URL: '{}'", splink);
                    } else {
                        logger->debug("Invalid Spotify button with URL: '{}'", splink);
                    }
                }
            }
        }

        if (logger) {
            logger->debug("Updating discordrp presence");
        }

        activity.SetAssets(assets);
        client->UpdateRichPresence(activity, [logger = this->logger](const discordpp::ClientResult &result) {
            if (!result.Successful()) {
                if (logger) {
                    logger->error("Discord RPC update failed ({}): {}", static_cast<int>(result.Type()),
                                  result.Error());
                }
            }
        });
    } else {
        if (logger) {
            logger->debug("Clearing Discord Rich Presence (missing metadata)");
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
                logger->warn("Error in discordrp refresh loop: {}", e.what());
            }
        }
        this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}