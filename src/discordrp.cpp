//
// Created by Jonathan on 26-Sep-25.
//

#include <stringutils.h>
#include <spdlog/spdlog.h>
#include "discordrp.h"

#include "constants.h"

Discordrp::Discordrp(MediaPlayer *player, const uint64_t &apikey, spdlog::logger *logger) {
    this->apple_music_ = player;
    this->client_id_ = apikey;
    running_ = true;
    this->refresh_thread_ = thread(&Discordrp::RefreshLoop, this);
    if (logger) {
        logger->debug("Refresh thread started for discordrp");
    }
    this->logger_ = logger;
    client_->SetApplicationId(client_id_);
    client_->Connect();
    if (logger) {
        logger->info("discordrp connection initialized");
    }
}

Discordrp::~Discordrp() {
    running_ = false;
    client_->Disconnect();
    if (refresh_thread_.joinable()) refresh_thread_.join();
    if (this->logger_) {
        logger_->info("discordrp Killed");
    }
}

void Discordrp::update() const {
    if (!apple_music_->GetTitle().empty() && !apple_music_->GetArtist().empty() && !apple_music_->GetAlbum().empty()) {
        discordpp::Activity activity;

        const string title = DiscordBounds(apple_music_->GetTitle(), "Unknown Song");
        const string artist = DiscordBounds(apple_music_->GetArtist(), "Unknown Artist");
        const string album = DiscordBounds(apple_music_->GetAlbum(), "Unknown Album");
        const string imglink = ConvertWString(apple_music_->GetImage());
        const string amlink = ConvertWString(apple_music_->GetAmLink());
        const string lfmlink = ConvertWString(apple_music_->GetLastFmLink());
        const string splink = ConvertWString(apple_music_->GetSpotifyLink());
        const bool playing = apple_music_->GetState();
        const uint64_t start_ts = apple_music_->GetStartTs();
        const uint64_t end_ts = apple_music_->GetEndTs();
        const uint64_t pause_ts = apple_music_->GetPauseTimer();

        // Basic info
        activity.SetName(artist);
        activity.SetDetails(title);
        activity.SetState(artist);
        activity.SetStatusDisplayType(discordpp::StatusDisplayTypes::Details);
        activity.SetType(discordpp::ActivityTypes::Listening);
        if (logger_) {
            logger_->debug("Activity (name, details, state): '{}' '{}' '{}'", artist + ConvertWString(L" \uf3b4"),
                           title,
                           artist);
        }

        // Images
        discordpp::ActivityAssets assets;
        assets.SetLargeImage(imglink);
        if (imglink != "default") {
            assets.SetLargeUrl(imglink);
        }
        assets.SetLargeText(album);
        if (logger_) {
            logger_->debug("Assets (large image, large text): '{}' '{}'", imglink, album);
        }

        // Time
        discordpp::ActivityTimestamps timestamps;
        if (playing) {
            timestamps.SetStart(start_ts);
            timestamps.SetEnd(end_ts);
            activity.SetTimestamps(timestamps);
            if (logger_) {
                logger_->debug("Timestamps (start, end): '{}' '{}'", apple_music_->GetStartTs(),
                               apple_music_->GetEndTs());
            }
        } else {
            timestamps.SetStart(pause_ts);
            activity.SetTimestamps(timestamps);
            assets.SetSmallImage("pause");
            assets.SetSmallText("Paused");
            if (logger_) {
                logger_->debug("Timestamps (paused at): '{}'", pause_ts);
            }
        }

        // Buttons
        if (!amlink.empty()) {
            discordpp::ActivityButton button1;
            button1.SetLabel("Apple Music");
            button1.SetUrl(amlink);
            activity.AddButton(button1);
            if (logger_) {
                if (button1) {
                    logger_->debug("Added Apple Music button with URL: '{}'", amlink);
                } else {
                    logger_->debug("Invalid Apple Music button with URL: '{}'", amlink);
                }
            }
        }
        if (!lfmlink.empty() || !splink.empty()) {
            discordpp::ActivityButton button2;
            if (!lfmlink.empty()) {
                button2.SetLabel("LastFM");
                button2.SetUrl(lfmlink);
                activity.AddButton(button2);
                if (logger_) {
                    if (button2) {
                        logger_->debug("Added LastFM button with URL: '{}'", lfmlink);
                    } else {
                        logger_->debug("Invalid LastFM button with URL: '{}'", lfmlink);
                    }
                }
            } else {
                button2.SetLabel("Spotify");
                button2.SetUrl(splink);
                activity.AddButton(button2);
                if (logger_) {
                    if (button2) {
                        logger_->debug("Added Spotify button with URL: '{}'", splink);
                    } else {
                        logger_->debug("Invalid Spotify button with URL: '{}'", splink);
                    }
                }
            }
        }

        if (logger_) {
            logger_->debug("Updating discordrp presence");
        }

        activity.SetAssets(assets);
        client_->UpdateRichPresence(activity, [logger = this->logger_](const discordpp::ClientResult &result) {
            if (!result.Successful()) {
                if (logger) {
                    logger->error("Discord RPC update failed ({}): {}", static_cast<int>(result.Type()),
                                  result.Error());
                }
            }
        });
    } else {
        if (logger_) {
            logger_->debug("Clearing Discord Rich Presence (missing metadata)");
        }
        client_->ClearRichPresence();
    }
}

void Discordrp::RefreshLoop() const {
    while (running_) {
        try {
            discordpp::RunCallbacks();
        } catch (exception &e) {
            if (logger_) {
                logger_->warn("Error in discordrp refresh loop: {}", e.what());
            }
        }
        this_thread::sleep_for(kDiscordRefreshInterval);
    }
}
