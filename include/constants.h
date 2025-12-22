//
// Created by Jonathan on 30-Nov-25.
//

#ifndef CONSTANTS_H
#define CONSTANTS_H

#include <string>
#include <chrono>

const std::wstring kVersion = L"1.1.3";

// Searches and Matching
constexpr double kMatchGenerosity = 0.6; // How close fuzzy matches should be to each other
constexpr int kMinSubstrLen = 5; // Minimum number of characters to enable the str inside other str match
const std::string kNumSearchResults = "5"; // How many results spotify/ last.fm should return
const std::wstring kDefaultImage = L"default";
const std::wstring kUnknownSource = L"unknown";

// Curl requests
constexpr long kCurlTimeout = 10; // How long requests should wait before timing out
constexpr long kCurlConnectTimeout = 5; // How long requests should wait for initial response

// Logging
constexpr std::chrono::seconds kLogFlushInterval(2); // How long between each log flush
constexpr int kMaxFiles = 10; // Max number of log files at one time

// Main loop
constexpr int kLoopRefreshInterval = 5000; // How many milliseconds between each metadata/ discord refresh
#define IDI_APPICON 101 // image
constexpr int kTrayMaxStrLen = 25;
// Max length of any string on the tray tool tip, DO NOT SET IT UNDER 10 OR YOU WILL HAVE TO CHANGE stringutils.cpp:Truncate(const wstring& input)!

// Regions
const std::vector<std::string> kRegionList = {
    "ae", "ag", "ai", "am", "ar", "at", "au", "az", "bb", "be",
    "bg", "bh", "bm", "bo", "br", "bs", "bw", "by", "bz", "ca",
    "cf", "ch", "ci", "cl", "cm", "cn", "co", "cr", "cz", "de",
    "dk", "dm", "do", "ec", "ee", "eg", "es", "fi", "fr", "gb",
    "gd", "ge", "gn", "gq", "gr", "gt", "gw", "gy", "hk", "hn",
    "hr", "hu", "id", "ie", "il", "in", "it", "jm", "jo", "jp",
    "kg", "kn", "kr", "kw", "ky", "kz", "la", "lc", "li", "lt",
    "lu", "lv", "ma", "md", "me", "mg", "mk", "ml", "mo", "ms",
    "mt", "mu", "mx", "my", "mz", "ne", "ng", "ni", "nl", "no",
    "nz", "om", "pa", "pe", "ph", "pl", "pr", "pt", "py", "qa",
    "ro", "ru", "sa", "se", "sg", "si", "sk", "sn", "sr", "sv",
    "tc", "th", "tj", "tm", "tn", "tr", "tt", "tw", "ua", "ug",
    "us", "uy", "uz", "vc", "ve", "vg", "vn", "za"
}; // All valid Apple Music Regions
const std::string kDefaultRegion = "ca"; // Default region

// Spotify
// Three-way matches weight mixing - These should add up to 1
constexpr double kArtistWeight = 0.4;
constexpr double kTitleWeight = 0.4;
constexpr double kAlbumWeight = 0.2;
constexpr int kSpotifyTokenLen = 16; // Length of the spotiy token - do not change!
constexpr int kSpotifyTokenValidity = 3600; // How long a token is valid for, in s
constexpr int kSpotifyRefreshInterval = 3550; // How long between each token refresh

// Last.FM
constexpr int kMaxScrobbleAttempts = 3; // Max tries to scrobble a song
constexpr int kMaxSetNowPlayingAttempts = 3; // Max tries to set last.fm's now playing
constexpr int kLfmMinTime = 30; // Min time for a song to be considered for scrobbling
constexpr double kLfmPercentage = 0.75; // Min percentage for a song to be scrobbled
constexpr int kLfmElapsedTime = 240; // Min time before a song is force scrobbled

// Apple Music Web Scraper
const std::string kTargetSize = "800x800bb-60";
// Apple Music scraping target image size - I recommend keeping it at this

// Discord
#define DISCORDPP_IMPLEMENTATION
constexpr uint64_t kDiscordApikey = 1358389458956976128; // Discord application api key
constexpr int kDiscordMaxStrLen = 128; // The max length of a string in a Discord activity is 128 - do not change!
constexpr std::chrono::milliseconds kDiscordRefreshInterval(10); // Recommended Discord value

// Database Keys
const std::string kRegionDbKey = "config:region";
const std::string kDiscordStateKey = "config:discord";
const std::string kLfmStateKey = "config:lastfm";
const std::wstring kSpotifyDbClientIdKey = L"musicpp/spotify_client_id";
const std::wstring kSpotifyDbClientSecretKey = L"musicpp/spotify_client_secret";
const std::wstring kSpotifyDbClientToken = L"musicpp/spotify_client_token";
const std::wstring kImgurDbClientIdKey = L"musicpp/imgur_client_id";
const std::wstring kLastFmDbApikey = L"musicpp/lastfm_api_key";
const std::wstring kLastFmDbSecret = L"musicpp/lastfm_secret";
const std::wstring kLastFmDbSessionKey = L"musicpp/lastfm_sessionkey";

// Others
constexpr uint64_t kWindowsTsConversion = 10'000'000;
// Windows gives us the current timestamp as nanoseconds, this converts to seconds
constexpr int kDbExpireTime = 7 * 24 * 60 * 60; // images/ links automatically expire in a week
constexpr uint64_t kInvalidTime = std::numeric_limits<uint64_t>::max();
constexpr int kSnapshotTypeDiscord = 1;
constexpr int kSnapshotTypeTray = 2;
constexpr int kSnapshotTypeTime = 3;

#endif