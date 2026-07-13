/**
 * @file enricher_test.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include "metadata/cache.hpp"
#include "metadata/enricher.hpp"
#include "metadata/sources/source.hpp"
#include "metadata/uploaders/uploader.hpp"
#include "types/track.hpp"

namespace {
Track makeTrack(const std::string &title = "Bohemian Rhapsody") {
    Track track;
    track.identity.title = title;
    track.identity.artist = "Queen";
    track.identity.album = "A Night at the Opera";
    track.status = Playing;
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
                ("musicpp_enricher_test_" + std::to_string(rng()));
    }

    ~TempDb() {
        std::error_code ec;
        remove_all(_path, ec);
    }

    TempDb(const TempDb &) = delete;

    TempDb &operator=(const TempDb &) = delete;

    [[nodiscard]] const std::filesystem::path &path() const { return _path; }

private:
    std::filesystem::path _path;
};

/**
 * A source handing back a scripted result, counting what it was asked.
 */
class FakeSource final : public MetadataWebSource {
public:
    FakeSource(std::string name, SearchResult result)
        : _name(std::move(name)), _result(std::move(result)) {
    }

    SearchResult searchTrack(const Track &track) override {
        ++calls;
        asked = track.identity;
        return _result;
    }

    std::string identify() override { return _name; }

    int calls = 0;
    TrackIdentity asked{};

private:
    std::string _name;
    SearchResult _result;
};

/**
 * An uploader handing back a scripted url, counting what it was asked to rehost.
 */
class FakeUploader final : public Uploader {
public:
    FakeUploader(std::string name, std::string url) : _name(std::move(name)),
                                                      _url(std::move(url)) {
    }

    UploadResult uploadImage(const std::vector<unsigned char> &bytes, ImageType type) override {
        ++calls;
        received = bytes;
        receivedType = type;
        return UploadResult{.image_url = _url};
    }

    std::string identify() override { return _name; }

    int calls = 0;
    std::vector<unsigned char> received{};
    ImageType receivedType = Static;

private:
    std::string _name;
    std::string _url;
};

SearchResult found(const std::string &image, const std::string &web) {
    return SearchResult{.image_url = image, .web_url = web, .image_type = Static};
}

/// What a source hands back when it could not match the track at all.
SearchResult missed() { return SearchResult{}; }

const std::vector<unsigned char> kThumbnail{0x89, 0x50, 0x4E, 0x47};
} // namespace

TEST_CASE("An image found by a source is returned", "[enricher]") {
    const TempDb db;
    MetadataCache cache(db.path());
    Enricher enricher(cache);

    auto source = std::make_shared<FakeSource>(
        "apple", found("https://img/queen.jpg", "https://apple/queen"));
    enricher.registerSource(source);

    const auto track = makeTrack();
    const auto enriched = enricher.enrich(track, std::nullopt);

    CHECK(enriched.image.url == "https://img/queen.jpg");
    CHECK(enriched.image.type == Static);
    CHECK(enriched.track.identity == track.identity);
    CHECK(source->calls == 1);
    CHECK(source->asked == track.identity);
}

TEST_CASE("A source with nothing left to offer is not called", "[enricher][cache]") {
    // The cache already holds this source's image and its link, so there is nothing to gain by
    // asking it again.
    const TempDb db;
    MetadataCache cache(db.path());

    const auto track = makeTrack();

    EnrichedTrack cached;
    cached.track = track;
    cached.image = ImageUrl{.url = "https://img/cached.jpg", .type = Static, .source = "apple"};
    cached.songUrls = {SongUrl{.url = "https://apple/queen", .source = "apple"}};
    cache.writeEntry(cached);

    Enricher enricher(cache);
    auto source = std::make_shared<FakeSource>("apple", found("https://img/fresh.jpg", ""));
    enricher.registerSource(source);

    const auto enriched = enricher.enrich(track, std::nullopt);

    CHECK(enriched.image.url == "https://img/cached.jpg");
    CHECK(source->calls == 0);
}

TEST_CASE("A cached image is not fetched again", "[enricher][cache]") {
    // A source new to the cache is still asked — for its link — but the image already held stands.
    const TempDb db;
    MetadataCache cache(db.path());

    const auto track = makeTrack();

    EnrichedTrack cached;
    cached.track = track;
    cached.image = ImageUrl{.url = "https://img/cached.jpg", .type = Static, .source = "apple"};
    cache.writeEntry(cached);

    Enricher enricher(cache);
    enricher.registerSource(std::make_shared<FakeSource>(
        "lastfm", found("https://img/fresh.jpg", "https://lastfm/queen")));

    const auto enriched = enricher.enrich(track, std::nullopt);

    CHECK(enriched.image.url == "https://img/cached.jpg");
    REQUIRE(enriched.songUrls.size() == 1);
    CHECK(enriched.songUrls.front().source == "lastfm");
}

TEST_CASE("What a source finds is persisted for the next enrichment", "[enricher][cache]") {
    const TempDb db;
    const auto track = makeTrack(); {
        MetadataCache cache(db.path());
        Enricher enricher(cache);
        enricher.registerSource(std::make_shared<FakeSource>(
            "apple", found("https://img/queen.jpg", "https://apple/queen")));
        const auto enriched = enricher.enrich(track, std::nullopt);
        REQUIRE(enriched.image.url == "https://img/queen.jpg");
    }

    // A fresh cache over the same directory, with a source that would answer differently.
    MetadataCache reopened(db.path());
    Enricher enricher(reopened);
    auto source = std::make_shared<FakeSource>("apple", found("https://img/other.jpg", ""));
    enricher.registerSource(source);

    const auto enriched = enricher.enrich(track, std::nullopt);

    CHECK(enriched.image.url == "https://img/queen.jpg");
    CHECK(source->calls == 0);
    REQUIRE(enriched.songUrls.size() == 1);
    CHECK(enriched.songUrls.front().url == "https://apple/queen");
}

TEST_CASE("Sources are tried in registration order", "[enricher]") {
    const TempDb db;
    MetadataCache cache(db.path());
    Enricher enricher(cache);

    auto first = std::make_shared<FakeSource>("apple", found("https://img/apple.jpg", ""));
    auto second = std::make_shared<FakeSource>("lastfm", found("https://img/lastfm.jpg", ""));
    enricher.registerSource(first);
    enricher.registerSource(second);

    const auto enriched = enricher.enrich(makeTrack(), std::nullopt);

    CHECK(enriched.image.url == "https://img/apple.jpg");
    CHECK(enriched.image.source == "apple");
}

TEST_CASE("A source that cannot match the track is passed over", "[enricher]") {
    const TempDb db;
    MetadataCache cache(db.path());
    Enricher enricher(cache);

    auto first = std::make_shared<FakeSource>("apple", missed());
    auto second = std::make_shared<FakeSource>("lastfm", found("https://img/lastfm.jpg", ""));
    enricher.registerSource(first);
    enricher.registerSource(second);

    const auto enriched = enricher.enrich(makeTrack(), std::nullopt);

    CHECK(first->calls == 1);
    CHECK(enriched.image.url == "https://img/lastfm.jpg");
    CHECK(enriched.image.source == "lastfm");
}

TEST_CASE("Song urls are collected from every source", "[enricher]") {
    const TempDb db;
    MetadataCache cache(db.path());
    Enricher enricher(cache);

    enricher.registerSource(std::make_shared<FakeSource>(
        "apple", found("https://img/apple.jpg", "https://apple/queen")));
    enricher.registerSource(std::make_shared<FakeSource>(
        "lastfm", found("https://img/lastfm.jpg", "https://lastfm/queen")));

    const auto enriched = enricher.enrich(makeTrack(), std::nullopt);

    // The image comes from the first source only, but a link is worth having from both.
    REQUIRE(enriched.songUrls.size() == 2);

    std::vector<std::string> sources;
    for (const auto &url : enriched.songUrls)
        sources.push_back(url.source);
    CHECK(std::ranges::find(sources, "apple") != sources.end());
    CHECK(std::ranges::find(sources, "lastfm") != sources.end());
}

TEST_CASE("A source is only ever listed once", "[enricher]") {
    // Enriching twice must not append the same link a second time. Only one cache may hold the
    // database at a time, so the first is closed before the second opens.
    const TempDb db;
    const auto track = makeTrack(); {
        MetadataCache cache(db.path());
        Enricher enricher(cache);
        enricher.registerSource(std::make_shared<FakeSource>(
            "apple", found("", "https://apple/queen")));
        const auto first = enricher.enrich(track, std::nullopt);
        REQUIRE(first.songUrls.size() == 1);
    }

    MetadataCache cache(db.path());
    Enricher enricher(cache);
    enricher.registerSource(std::make_shared<FakeSource>(
        "apple", found("", "https://apple/queen")));
    const auto second = enricher.enrich(track, std::nullopt);

    CHECK(second.songUrls.size() == 1);
}

TEST_CASE("With no source image the thumbnail is rehosted", "[enricher][uploader]") {
    const TempDb db;
    MetadataCache cache(db.path());
    Enricher enricher(cache);

    enricher.registerSource(std::make_shared<FakeSource>("apple", missed()));
    auto uploader = std::make_unique<FakeUploader>("imgur", "https://imgur/abc.png");
    auto *seen = uploader.get();
    enricher.registerUploader(std::move(uploader));

    const auto enriched = enricher.enrich(makeTrack(), kThumbnail);

    CHECK(enriched.image.url == "https://imgur/abc.png");
    CHECK(seen->calls == 1);
    CHECK(seen->received == kThumbnail);
}

TEST_CASE("An image from a source is preferred over rehosting", "[enricher][uploader]") {
    const TempDb db;
    MetadataCache cache(db.path());
    Enricher enricher(cache);

    enricher.registerSource(std::make_shared<FakeSource>("apple", found("https://img/a.jpg", "")));
    auto uploader = std::make_unique<FakeUploader>("imgur", "https://imgur/abc.png");
    auto *seen = uploader.get();
    enricher.registerUploader(std::move(uploader));

    const auto enriched = enricher.enrich(makeTrack(), kThumbnail);

    CHECK(enriched.image.url == "https://img/a.jpg");
    CHECK(seen->calls == 0);
}

TEST_CASE("Without a thumbnail there is nothing to rehost", "[enricher][uploader]") {
    const TempDb db;
    MetadataCache cache(db.path());
    Enricher enricher(cache);

    enricher.registerSource(std::make_shared<FakeSource>("apple", missed()));
    auto uploader = std::make_unique<FakeUploader>("imgur", "https://imgur/abc.png");
    auto *seen = uploader.get();
    enricher.registerUploader(std::move(uploader));

    const auto enriched = enricher.enrich(makeTrack(), std::nullopt);

    CHECK(seen->calls == 0);
    CHECK(enriched.image.url.empty());
}

TEST_CASE("A total failure to enrich is survivable", "[enricher]") {
    const TempDb db;
    MetadataCache cache(db.path());
    const Enricher enricher(cache); // No sources, no uploaders.

    const auto track = makeTrack();
    const auto enriched = enricher.enrich(track, std::nullopt);

    CHECK(enriched.image.url.empty());
    CHECK(enriched.songUrls.empty());
    CHECK(enriched.track.identity == track.identity);
    CHECK(enriched.track.status == Playing);
}