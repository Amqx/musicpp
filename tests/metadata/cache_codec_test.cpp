/**
 * @file cache_codec_test.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include "metadata/cache_codec.hpp"

namespace {
Track makeTrack(const std::string &title, const std::string &artist, const std::string &album) {
    Track track;
    track.identity.title = title;
    track.identity.artist = artist;
    track.identity.album = album;
    return track;
}

}

TEST_CASE("keys are prefixed per value type and separate image from urls", "[codec]") {
    const Track track = makeTrack("Bohemian Rhapsody", "Queen", "A Night at the Opera");

    REQUIRE(cache_codec::imageKey(track) == "img|Bohemian Rhapsody|Queen|A Night at the Opera");
    REQUIRE(cache_codec::urlKey(track) == "url|Bohemian Rhapsody|Queen|A Night at the Opera");
}

TEST_CASE("pipes in track metadata cannot forge a key boundary", "[codec]") {
    const Track track = makeTrack("a|b", "c", "d");

    REQUIRE(cache_codec::imageKey(track) == "img|a-b|c|d");
}

TEST_CASE("image values round-trip with their write instant", "[codec]") {
    const ImageUrl image{"https://i.imgur.com/abc.png", Animated, "imgur"};
    const auto written = cache_codec::nowSeconds();

    const auto parsed =
        cache_codec::parseImageValue(cache_codec::createImageValue(image, written));

    REQUIRE(parsed.has_value());
    REQUIRE(parsed->image.url == image.url);
    REQUIRE(parsed->image.type == Animated);
    REQUIRE(parsed->image.source == "imgur");
    REQUIRE(parsed->written_at == written);
}

TEST_CASE("a truncated image value is rejected rather than half-parsed", "[codec]") {
    const ImageUrl image{"https://i.imgur.com/abc.png", Static, "imgur"};
    const std::string raw = cache_codec::createImageValue(image, cache_codec::nowSeconds());

    REQUIRE_FALSE(cache_codec::parseImageValue("").has_value());
    // Cuts inside the timestamp, and inside the source length that follows it.
    REQUIRE_FALSE(cache_codec::parseImageValue(raw.substr(0, 4)).has_value());
    REQUIRE_FALSE(cache_codec::parseImageValue(raw.substr(0, 11)).has_value());
}

TEST_CASE("an image is fresh until the ttl elapses", "[codec]") {
    const auto now = cache_codec::nowSeconds();

    SECTION("just written") {
        REQUIRE(cache_codec::isFresh(now, now));
    }
    SECTION("one second short of the ttl") {
        REQUIRE(cache_codec::isFresh(now - cache_codec::kImageTtl + std::chrono::seconds{1}, now));
    }
    SECTION("exactly at the ttl") {
        REQUIRE_FALSE(cache_codec::isFresh(now - cache_codec::kImageTtl, now));
    }
    SECTION("long past the ttl") {
        REQUIRE_FALSE(cache_codec::isFresh(now - std::chrono::days{20}, now));
    }
}

TEST_CASE("a clock jump backwards cannot invalidate the cache", "[codec]") {
    const auto now = cache_codec::nowSeconds();

    // Written "in the future" -- the system clock moved backwards since. Counts as fresh: a
    // stale clock must not evict every cached image at once.
    REQUIRE(cache_codec::isFresh(now + std::chrono::days{30}, now));
}

TEST_CASE("song url lists round-trip", "[codec]") {
    const std::vector<SongUrl> urls{
        {"https://music.apple.com/song/1", "applemusic"},
        {"https://last.fm/song/1", "lastfm"},
    };

    const auto parsed = cache_codec::parseUrlValue(cache_codec::createUrlValue(urls));

    REQUIRE(parsed.size() == 2);
    REQUIRE(parsed[0].url == "https://music.apple.com/song/1");
    REQUIRE(parsed[0].source == "applemusic");
    REQUIRE(parsed[1].source == "lastfm");
}

TEST_CASE("merging song urls dedupes and drops empties", "[codec]") {
    const std::vector<SongUrl> existing{{"https://music.apple.com/song/1", "applemusic"}};
    const std::vector<SongUrl> incoming{
        {"https://music.apple.com/song/1", "applemusic"}, // duplicate
        {"", "lastfm"}, // empty url
        {"https://last.fm/song/1", "lastfm"}, // new
    };

    const auto merged = cache_codec::mergeSongUrls(existing, incoming);

    REQUIRE(merged.size() == 2);
    REQUIRE(merged[1].source == "lastfm");
    REQUIRE(merged[1].url == "https://last.fm/song/1");
}