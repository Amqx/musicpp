//
// Created by Jonathan on 26-Sep-25.
//

#include <stringutils.h>
#include <spdlog/spdlog.h>
#include "discordrp.h"
#include "constants.h"

Discordrp::Discordrp(MediaPlayer *player, const uint64_t &apikey, leveldb::DB *db, spdlog::logger *logger) {
    this->apple_music_ = player;
    this->client_id_ = apikey;
    this->db_ = db;
    this->refresh_thread_ = thread(&Discordrp::RefreshLoop, this);
    if (logger) {
        logger->debug("Refresh thread started for discordrp");
    }
    running_ = true;
    this->enabled_ = true;
    if (db_) {
        string result;
        if (const auto status = db_->Get(leveldb::ReadOptions(), kDiscordStateKey, &result); !status.ok()) {
            if (logger) logger->info("Did not find previous Discord state, resetting to active");
        } else {
            if (result == "false") {
                enabled_ = false;
                if (logger) logger->info("DB Discord state pulled: Set to false");
            } else {
                if (logger) logger->info("DB Discord state pulled: Set to true");
            }
        }
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
    if (db_) {
        if (enabled_) {
            db_->Put(leveldb::WriteOptions(), kDiscordStateKey, "true");
        } else {
            db_->Put(leveldb::WriteOptions(), kDiscordStateKey, "false");
        }
    }
    if (refresh_thread_.joinable()) refresh_thread_.join();
    if (this->logger_) {
        logger_->info("Saved Discord state: "s + (enabled_ ? "true" : "false"));
        logger_->info("discordrp Killed");
    }
}

void Discordrp::update() const {
    const Snapshot metadata = apple_music_->GetSnapshot(kSnapshotTypeDiscord);
    if (!metadata.title.empty() && !metadata.artist.empty() && !metadata.album.empty() && enabled_) {
        discordpp::Activity activity;

        const string title = DiscordBounds(metadata.title, "Unknown Song");
        const string artist = DiscordBounds(metadata.artist, "Unknown Artist");
        const string album = DiscordBounds(metadata.album, "Unknown Album");
        const string imglink = ConvertWString(metadata.image);
        const string amlink = ConvertWString(metadata.amlink);
        const string lfmlink = ConvertWString(metadata.lfmlink);
        const string splink = ConvertWString(metadata.splink);
        const bool playing = metadata.state;
        const uint64_t start_ts = metadata.start_ts;
        const uint64_t end_ts = metadata.end_ts;
        const uint64_t pause_ts = metadata.pause_timer;

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
                logger_->debug("Timestamps (start, end): '{}' '{}'", start_ts, end_ts);
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
        if (!enabled_) {
            if (logger_) {
                logger_->debug("Clearing Discord Rich Presence (disabled)");
            }
        } else {
            if (logger_) {
                logger_->debug("Clearing Discord Rich Presence (missing metadata)");
            }
        }
        client_->ClearRichPresence();
    }
}

void Discordrp::toggle() {
    enabled_ = !enabled_;
}

bool Discordrp::GetState() const {
    return enabled_;
}

void Discordrp::RefreshLoop() const {
    while (running_ && enabled_) {
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
