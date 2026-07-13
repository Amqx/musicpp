/**
 * @file orchestrator_test.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>
#include "discord/presence.hpp"
#include "metadata/cache.hpp"
#include "metadata/enricher.hpp"
#include "metadata/sources/source.hpp"
#include "orchestrator/orchestrator.hpp"
#include "system/poller.hpp"
#include "types/track.hpp"

using namespace std::chrono_literals;

namespace {
/// The clock TrackTiming::set() measures against, as the real poller feeds it.
int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

/**
 * A track four minutes long, already played the given way in.
 */
Track makeTrack(const std::string &title, const std::chrono::seconds position = 0s,
                const TrackStatus status = Playing) {
    Track track;
    track.identity.title = title;
    track.identity.artist = "Queen";
    track.identity.album = "A Night at the Opera";
    track.status = status;

    const auto start = nowNs() - std::chrono::nanoseconds(position).count();
    track.timing.set(start, start + std::chrono::nanoseconds(4min).count());
    return track;
}

/// What the poller reports when no player is running at all.
Track noPlayer() { return Track{}; }

/**
 * A leveldb directory under temp, removed on destruction. Never the real song_db.
 */
class TempDb {
public:
    TempDb() {
        static std::mt19937_64 rng{std::random_device{}()};
        _path = std::filesystem::temp_directory_path() /
                ("musicpp_orch_test_" + std::to_string(rng()));
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
 * A poller playing out a scripted sequence of cycles, holding on the last one.
 */
class FakePoller final : public Poller {
public:
    std::tuple<Track, std::optional<std::vector<unsigned char> > > poll() override {
        ++polls;
        if (script.empty()) return {Track{}, std::nullopt};
        const auto &track = script[std::min(_cycle, script.size() - 1)];
        ++_cycle;
        return {track, thumbnail};
    }

    std::string identify() override { return "fake"; }

    std::vector<Track> script{};
    std::optional<std::vector<unsigned char> > thumbnail{};
    int polls = 0;

private:
    std::size_t _cycle = 0;
};

/**
 * A presence recording everything it was handed.
 */
class FakePresence final : public Presence {
public:
    void setPresence(const EnrichedTrack &track) const override {
        published.push_back(track);
    }

    void clearPresence() const override { ++cleared; }

    [[nodiscard]] const EnrichedTrack &last() const { return published.back(); }

    mutable std::vector<EnrichedTrack> published{};
    mutable int cleared = 0;
};

/**
 * A source counting how often the orchestrator asked for an enrichment.
 */
class CountingSource final : public MetadataWebSource {
public:
    SearchResult searchTrack(const Track &track) override {
        ++calls;
        return SearchResult{
            .image_url = "https://img/" + track.identity.title + ".jpg",
            .web_url = "https://apple/" + track.identity.title,
            .image_type = Static
        };
    }

    std::string identify() override { return "apple"; }

    int calls = 0;
};

/**
 * The whole poll → enrich → publish stack, with the edges faked out.
 */
class Rig {
public:
    Rig() : _cache(_db.path()) {
        auto poller = std::make_unique<FakePoller>();
        auto presence = std::make_unique<FakePresence>();
        auto enricher = std::make_unique<Enricher>(_cache);

        source = std::make_shared<CountingSource>();
        enricher->registerSource(source);

        this->poller = poller.get();
        this->presence = presence.get();

        orchestrator.registerPoller(std::move(poller));
        orchestrator.registerRichPresence(std::move(presence));
        orchestrator.registerEnricher(std::move(enricher));
    }

    /// Runs one poll cycle, as main() does every 5 seconds.
    void cycle(const int times = 1) {
        for (int i = 0; i < times; ++i) orchestrator.run();
    }

    Orchestrator orchestrator{};
    FakePoller *poller = nullptr;
    FakePresence *presence = nullptr;
    std::shared_ptr<CountingSource> source{};

private:
    TempDb _db;
    MetadataCache _cache;
};
} // namespace

TEST_CASE("A playing track is enriched and published", "[orchestrator]") {
    Rig rig;
    rig.poller->script = {makeTrack("Bohemian Rhapsody")};

    rig.cycle();

    REQUIRE(rig.presence->published.size() == 1);
    const auto &published = rig.presence->last();
    CHECK(published.track.identity.title == "Bohemian Rhapsody");
    CHECK(published.image.url == "https://img/Bohemian Rhapsody.jpg");
    CHECK(rig.source->calls == 1);
    CHECK(rig.presence->cleared == 0);
}

TEST_CASE("No player running publishes nothing", "[orchestrator]") {
    // Nothing was ever presented, so there is nothing to tear down either: the cycle is a no-op
    // rather than a needless clear on every poll while the player is closed.
    Rig rig;
    rig.poller->script = {noPlayer()};

    rig.cycle(3);

    CHECK(rig.presence->published.empty());
    CHECK(rig.presence->cleared == 0);
    CHECK(rig.source->calls == 0);
}

TEST_CASE("A track that stops playing tears the presence down", "[orchestrator]") {
    Rig rig;
    rig.poller->script = {makeTrack("Bohemian Rhapsody"), noPlayer()};

    rig.cycle(2);

    CHECK(rig.presence->published.size() == 1);
    CHECK(rig.presence->cleared >= 1);
}

TEST_CASE("The same track is not enriched twice", "[orchestrator]") {
    Rig rig;
    rig.poller->script = {
        makeTrack("Bohemian Rhapsody", 0s),
        makeTrack("Bohemian Rhapsody", 5s),
        makeTrack("Bohemian Rhapsody", 10s)
    };

    rig.cycle(3);

    CHECK(rig.source->calls == 1);
    CHECK(rig.presence->published.size() == 3);
}

TEST_CASE("A different track is enriched afresh", "[orchestrator]") {
    Rig rig;
    rig.poller->script = {makeTrack("Bohemian Rhapsody"), makeTrack("Love of My Life")};

    rig.cycle(2);

    CHECK(rig.source->calls == 2);
    REQUIRE(rig.presence->published.size() == 2);
    CHECK(rig.presence->last().track.identity.title == "Love of My Life");
    CHECK(rig.presence->last().image.url == "https://img/Love of My Life.jpg");
}

TEST_CASE("A track started over counts as a new play", "[orchestrator][restart]") {
    // Same track, but the position has jumped backwards past kRestartThreshold: the user hit
    // replay. The presence must follow the new position rather than the stale one.
    Rig rig;
    rig.poller->script = {
        makeTrack("Bohemian Rhapsody", 3min),
        makeTrack("Bohemian Rhapsody", 0s)
    };

    rig.cycle(2);

    REQUIRE(rig.presence->published.size() == 2);
    const auto replayed = rig.presence->last().track.timing.current();
    CHECK(replayed < 30s);

    // It is the same song, so the image it was already enriched with still stands.
    CHECK(rig.source->calls == 1);
}

TEST_CASE("Ordinary progress is not mistaken for a restart", "[orchestrator][restart]") {
    Rig rig;
    rig.poller->script = {
        makeTrack("Bohemian Rhapsody", 60s),
        makeTrack("Bohemian Rhapsody", 65s),
        makeTrack("Bohemian Rhapsody", 70s)
    };

    rig.cycle(3);

    CHECK(rig.source->calls == 1);
    REQUIRE(rig.presence->published.size() == 3);
    CHECK(rig.presence->last().track.timing.current() > 60s);
}

TEST_CASE("A pause records the instant it began", "[orchestrator][pause]") {
    Rig rig;
    rig.poller->script = {
        makeTrack("Bohemian Rhapsody", 30s, Playing),
        makeTrack("Bohemian Rhapsody", 30s, Paused)
    };

    rig.cycle();
    CHECK_FALSE(rig.presence->last().pause.since.has_value());

    rig.cycle();
    REQUIRE(rig.presence->last().pause.since.has_value());
}

TEST_CASE("A pause holds the instant it began across cycles", "[orchestrator][pause]") {
    // No single poll can tell when the pause started, so the orchestrator must remember it. If it
    // re-stamped the instant every cycle, a long pause would keep resetting to now.
    Rig rig;
    rig.poller->script = {
        makeTrack("Bohemian Rhapsody", 30s, Paused),
        makeTrack("Bohemian Rhapsody", 30s, Paused),
        makeTrack("Bohemian Rhapsody", 30s, Paused)
    };

    rig.cycle();
    REQUIRE(rig.presence->last().pause.since.has_value());
    const auto since = *rig.presence->last().pause.since;

    rig.cycle(2);

    REQUIRE(rig.presence->last().pause.since.has_value());
    CHECK(*rig.presence->last().pause.since == since);
}

TEST_CASE("Resuming clears the pause", "[orchestrator][pause]") {
    Rig rig;
    rig.poller->script = {
        makeTrack("Bohemian Rhapsody", 30s, Paused),
        makeTrack("Bohemian Rhapsody", 30s, Playing)
    };

    rig.cycle();
    REQUIRE(rig.presence->last().pause.since.has_value());

    rig.cycle();
    CHECK_FALSE(rig.presence->last().pause.since.has_value());
}

TEST_CASE("An orchestrator with nothing registered does not fall over", "[orchestrator]") {
    Orchestrator orchestrator;
    orchestrator.run();
    SUCCEED("A cycle with no poller is a no-op");
}
