/**
 * @file cache_test.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <leveldb/db.h>
#include <memory>
#include <random>
#include "metadata/cache.hpp"
#include "metadata/cache_codec.hpp"

namespace {
Track makeTrack(const std::string &title) {
    Track track;
    track.identity.title = title;
    track.identity.artist = "Queen";
    track.identity.album = "A Night at the Opera";
    return track;
}

/**
 * A leveldb directory under temp, removed on destruction. Never the real song_db.
 */
class TempDb {
public:
    TempDb() {
        static std::mt19937_64 rng{std::random_device{}()};
        _path = std::filesystem::temp_directory_path() /
                ("musicpp_test_" + std::to_string(rng()));
    }

    ~TempDb() {
        std::error_code ec;
        remove_all(_path, ec);
    }

    TempDb(const TempDb &) = delete;

    TempDb &operator=(const TempDb &) = delete;

    [[nodiscard]] const std::filesystem::path &path() const { return _path; }

    /**
     * Writes a raw key/value straight into the database, bypassing MetadataCache.
     */
    void put(const std::string &key, const std::string &value) const {
        const auto db = openRaw();
        REQUIRE(db->Put(leveldb::WriteOptions(), key, value).ok());
    }

    [[nodiscard]] bool has(const std::string &key) const {
        const auto db = openRaw();
        std::string value;
        return db->Get(leveldb::ReadOptions(), key, &value).ok();
    }

private:
    [[nodiscard]] std::unique_ptr<leveldb::DB> openRaw() const {
        leveldb::DB *raw = nullptr;
        leveldb::Options options;
        options.create_if_missing = true;
        REQUIRE(leveldb::DB::Open(options, _path.string(), &raw).ok());
        return std::unique_ptr<leveldb::DB>(raw);
    }

    std::filesystem::path _path;
};

const ImageUrl kImage{"https://i.imgur.com/abc.png", Static, "imgur"};
const std::vector<SongUrl> kUrls{{"https://music.apple.com/song/1", "applemusic"}};

}

TEST_CASE("a fresh image survives a round trip through the cache", "[cache]") {
    const TempDb temp;
    const Track track = makeTrack("Bohemian Rhapsody");

    EnrichedTrack enriched;
    enriched.track = track;
    enriched.image = kImage;
    enriched.songUrls = kUrls; {
        const MetadataCache cache(temp.path());
        cache.writeEntry(enriched);
    }

    const MetadataCache cache(temp.path());
    const auto found = cache.findEntry(track);

    REQUIRE(found.has_value());
    REQUIRE(found->image.url == kImage.url);
    REQUIRE(found->songUrls.size() == 1);
}

TEST_CASE("an expired image is withheld, but its song urls are not", "[cache]") {
    const TempDb temp;
    const Track track = makeTrack("Bohemian Rhapsody");
    const auto expired = cache_codec::nowSeconds() - std::chrono::days{20};

    temp.put(cache_codec::imageKey(track), cache_codec::createImageValue(kImage, expired));
    temp.put(cache_codec::urlKey(track), cache_codec::createUrlValue(kUrls));

    const MetadataCache cache(temp.path());
    const auto found = cache.findEntry(track);

    // The entry still resolves: the enricher must keep the links and re-fetch only the art.
    REQUIRE(found.has_value());
    REQUIRE(found->image.url.empty());
    REQUIRE(found->songUrls.size() == 1);
    REQUIRE(found->songUrls[0].source == "applemusic");
}

TEST_CASE("an expired image with no song urls is a cache miss", "[cache]") {
    const TempDb temp;
    const Track track = makeTrack("Bohemian Rhapsody");
    const auto expired = cache_codec::nowSeconds() - std::chrono::days{20};

    temp.put(cache_codec::imageKey(track), cache_codec::createImageValue(kImage, expired));

    const MetadataCache cache(temp.path());

    REQUIRE_FALSE(cache.findEntry(track).has_value());
}

TEST_CASE("opening the cache sweeps expired image rows", "[cache]") {
    const TempDb temp;
    const Track stale = makeTrack("Stale Track");
    const Track fresh = makeTrack("Fresh Track");
    const auto now = cache_codec::nowSeconds();

    temp.put(cache_codec::imageKey(stale),
             cache_codec::createImageValue(kImage, now - std::chrono::days{20}));
    temp.put(cache_codec::urlKey(stale), cache_codec::createUrlValue(kUrls));
    temp.put(cache_codec::imageKey(fresh), cache_codec::createImageValue(kImage, now)); {
        const MetadataCache cache(temp.path()); // sweeps on open
    }

    REQUIRE_FALSE(temp.has(cache_codec::imageKey(stale)));
    // Song urls carry no timestamp and must survive the sweep untouched.
    REQUIRE(temp.has(cache_codec::urlKey(stale)));
    REQUIRE(temp.has(cache_codec::imageKey(fresh)));
}

TEST_CASE("opening the cache sweeps malformed image rows", "[cache]") {
    const TempDb temp;
    const Track track = makeTrack("Corrupt Track");

    temp.put(cache_codec::imageKey(track), "not a valid image value"); {
        const MetadataCache cache(temp.path());
    }

    REQUIRE_FALSE(temp.has(cache_codec::imageKey(track)));
}