/**
 * @file scrobble_driver_test.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#include <algorithm>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "orchestrator/scrobble_driver.hpp"
#include "types/track.hpp"

using namespace std::chrono_literals;

namespace {
/// The clock TrackTiming::set() measures against, as the real poller feeds it.
int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

/**
 * A track that began the given time ago and runs for the given length (four minutes by default).
 */
Track makeTrack(const std::string &title = "Bohemian Rhapsody",
                const std::chrono::seconds playedFor = 0s,
                const std::chrono::seconds length = 4min) {
    Track track;
    track.identity.title = title;
    track.identity.artist = "Queen";
    track.identity.album = "A Night at the Opera";
    track.status = Playing;

    const auto start = nowNs() - std::chrono::nanoseconds(playedFor).count();
    track.timing.set(start, start + std::chrono::nanoseconds(length).count());
    return track;
}

/**
 * A scrobbler recording every call the driver makes, answering however the test scripted it.
 * Calls arrive on the worker thread, so every field is guarded.
 */
class FakeScrobbler final : public Scrobbler {
public:
    explicit FakeScrobbler(std::string name) : _name(std::move(name)) {
    }

    bool authed() const override { return isAuthed.load(); }

    bool authenticateUser() override {
        ++authCalls;
        return isAuthed.load();
    }

    bool scrobble(const Track &track) const override {
        std::lock_guard lock(_mutex);
        _scrobbled.push_back(track.identity);
        std::this_thread::sleep_for(latency.load());
        return acceptScrobble.load();
    }

    bool setPlaying(const Track &track) const override {
        std::lock_guard lock(_mutex);
        _playing.push_back(track.identity);
        std::this_thread::sleep_for(latency.load());
        return acceptNowPlaying.load();
    }

    std::string identify() override { return _name; }

    [[nodiscard]] std::size_t scrobbles() const {
        std::lock_guard lock(_mutex);
        return _scrobbled.size();
    }

    [[nodiscard]] std::size_t nowPlayings() const {
        std::lock_guard lock(_mutex);
        return _playing.size();
    }

    [[nodiscard]] std::vector<TrackIdentity> playing() const {
        std::lock_guard lock(_mutex);
        return _playing;
    }

    [[nodiscard]] std::vector<TrackIdentity> scrobbled() const {
        std::lock_guard lock(_mutex);
        return _scrobbled;
    }

    std::atomic<bool> isAuthed{true};
    std::atomic<bool> acceptNowPlaying{true};
    std::atomic<bool> acceptScrobble{true};
    std::atomic<std::chrono::milliseconds> latency{0ms};
    std::atomic<int> authCalls{0};

private:
    std::string _name;
    mutable std::mutex _mutex{};
    mutable std::vector<TrackIdentity> _playing{};
    mutable std::vector<TrackIdentity> _scrobbled{};
};

/// A schedule that plays out in milliseconds rather than half a minute.
ScrobbleSchedule fastSchedule() {
    return ScrobbleSchedule{
        .scrobbleRetry = 20ms,
        .nowPlayingRetry = 20ms,
        .nowPlayingRefresh = 50ms,
        .nowPlayingAttempts = 3
    };
}

/**
 * Ticks the driver until the condition holds, standing in for the poll loop.
 * @return Whether the condition came true before the timeout.
 */
template <typename Predicate>
bool pumpUntil(ScrobbleDriver &driver, const Track &track, Predicate done,
               const std::chrono::milliseconds timeout = 3s) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        driver.tick(track);
        if (done())
            return true;
        std::this_thread::sleep_for(2ms);
    }
    driver.tick(track);
    return done();
}

/**
 * Ticks the driver for a while, for tests asserting something does NOT happen.
 */
void pumpFor(ScrobbleDriver &driver, const Track &track,
             const std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        driver.tick(track);
        std::this_thread::sleep_for(2ms);
    }
}
} // namespace

TEST_CASE("A new play sends a now-playing update", "[scrobbler]") {
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(scrobbler);

    const auto track = makeTrack();
    driver.reset();

    REQUIRE(pumpUntil(driver, track, [&] { return scrobbler->nowPlayings() >= 1; }));

    const auto playing = scrobbler->playing();
    REQUIRE(playing.size() == 1);
    CHECK(playing.front() == track.identity);
}

TEST_CASE("An accepted now-playing update is not repeated within the refresh interval",
          "[scrobbler]") {
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    auto schedule = fastSchedule();
    schedule.nowPlayingRefresh = 10s; // Far longer than the pump: no refresh should intrude.
    ScrobbleDriver driver(schedule);
    driver.registerScrobbler(scrobbler);

    const auto track = makeTrack();
    driver.reset();
    REQUIRE(pumpUntil(driver, track, [&] { return scrobbler->nowPlayings() >= 1; }));

    pumpFor(driver, track, 100ms);

    CHECK(scrobbler->nowPlayings() == 1);
}

TEST_CASE("A rejected now-playing update is retried", "[scrobbler]") {
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    scrobbler->acceptNowPlaying = false;

    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(scrobbler);

    const auto track = makeTrack();
    driver.reset();

    CHECK(pumpUntil(driver, track, [&] { return scrobbler->nowPlayings() >= 2; }));
}

TEST_CASE("A now-playing update gives up after the allowed attempts", "[scrobbler]") {
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    scrobbler->acceptNowPlaying = false;

    const auto schedule = fastSchedule();
    ScrobbleDriver driver(schedule);
    driver.registerScrobbler(scrobbler);

    const auto track = makeTrack();
    driver.reset();

    REQUIRE(pumpUntil(driver, track, [&] {
        return scrobbler->nowPlayings() >= static_cast<std::size_t>(schedule.nowPlayingAttempts);
        }));

    // Well past the retry interval: the play has given up on it.
    pumpFor(driver, track, 200ms);

    CHECK(scrobbler->nowPlayings() == static_cast<std::size_t>(schedule.nowPlayingAttempts));
}

TEST_CASE("A track played past half its length is scrobbled", "[scrobbler]") {
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(scrobbler);

    // A four-minute track, already two minutes in: past the halfway mark, so it qualifies.
    const auto track = makeTrack("Bohemian Rhapsody", 2min);
    driver.reset();

    CHECK(pumpUntil(driver, track, [&] { return scrobbler->scrobbles() >= 1; }));
}

TEST_CASE("A track not played far enough is not scrobbled", "[scrobbler]") {
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(scrobbler);

    // Thirty seconds into a four-minute track: short of both the halfway mark and the 240s cap.
    const auto track = makeTrack("Bohemian Rhapsody", 30s);
    driver.reset();

    pumpFor(driver, track, 150ms);
    CHECK(scrobbler->scrobbles() == 0);
    // The now-playing update does not wait on playback, so it still went out.
    CHECK(scrobbler->nowPlayings() >= 1);
}

TEST_CASE("A long track is scrobbled once it passes the play cap", "[scrobbler]") {
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(scrobbler);

    // A ten-minute track: halfway (5 min) is beyond the 240s cap, so the cap is what fires.
    // Four minutes and a second in, it is past the cap but nowhere near halfway.
    const auto track = makeTrack("Jazz Odyssey", 4min + 1s, 10min);
    driver.reset();

    CHECK(pumpUntil(driver, track, [&] { return scrobbler->scrobbles() >= 1; }));
}

TEST_CASE("A track shorter than the minimum length is never scrobbled", "[scrobbler]") {
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(scrobbler);

    // A twenty-second track played almost to its end: past halfway, but under Last.fm's 30s floor.
    const auto track = makeTrack("Her Majesty", 19s, 20s);
    driver.reset();

    pumpFor(driver, track, 150ms);
    CHECK(scrobbler->scrobbles() == 0);
}

TEST_CASE("An accepted scrobble is not repeated", "[scrobbler]") {
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(scrobbler);

    const auto track = makeTrack("Bohemian Rhapsody", 2min);
    driver.reset();
    REQUIRE(pumpUntil(driver, track, [&] { return scrobbler->scrobbles() >= 1; }));

    pumpFor(driver, track, 150ms);

    CHECK(scrobbler->scrobbles() == 1);
    const auto scrobbled = scrobbler->scrobbled();
    REQUIRE(scrobbled.size() == 1);
    CHECK(scrobbled.front() == track.identity);
}

TEST_CASE("A rejected scrobble is retried", "[scrobbler]") {
    // A scrobbler rejects until the track has been played far enough, so a rejection must not be
    // the end of it.
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    scrobbler->acceptScrobble = false;

    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(scrobbler);

    // Two minutes in, so the play has qualified and only the network is holding the scrobble back.
    const auto track = makeTrack("Bohemian Rhapsody", 2min);
    driver.reset();

    REQUIRE(pumpUntil(driver, track, [&] { return scrobbler->scrobbles() >= 3; }));

    // And once it is finally accepted, it stops.
    scrobbler->acceptScrobble = true;
    const auto before = scrobbler->scrobbles();
    REQUIRE(pumpUntil(driver, track, [&] { return scrobbler->scrobbles() > before; }));

    const auto accepted = scrobbler->scrobbles();
    pumpFor(driver, track, 150ms);
    CHECK(scrobbler->scrobbles() == accepted);
}

TEST_CASE("A scrobbler is only ever asked one thing at a time", "[scrobbler]") {
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    scrobbler->latency = 100ms; // Slow enough that many ticks pass while it is in flight.

    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(scrobbler);

    const auto track = makeTrack();
    driver.reset();

    // Ticking hard while the first now-playing is still with the worker must not pile up calls.
    pumpFor(driver, track, 60ms);
    CHECK(scrobbler->nowPlayings() <= 1);
}

TEST_CASE("Every registered scrobbler is driven", "[scrobbler]") {
    auto first = std::make_shared<FakeScrobbler>("lastfm");
    auto second = std::make_shared<FakeScrobbler>("libre");

    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(first);
    driver.registerScrobbler(second);

    // Two minutes in, so both are due a scrobble as well as a now-playing update.
    const auto track = makeTrack("Bohemian Rhapsody", 2min);
    driver.reset();

    CHECK(pumpUntil(driver, track, [&] {
        return first->nowPlayings() >= 1 && second->nowPlayings() >= 1;
        }));
    CHECK(pumpUntil(driver, track, [&] {
        return first->scrobbles() >= 1 && second->scrobbles() >= 1;
        }));
}

TEST_CASE("A scrobbler with no session is left alone", "[scrobbler][auth]") {
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    scrobbler->isAuthed = false;

    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(scrobbler);

    const auto track = makeTrack();
    driver.reset();

    pumpFor(driver, track, 150ms);

    CHECK(scrobbler->nowPlayings() == 0);
    CHECK(scrobbler->scrobbles() == 0);
}

TEST_CASE("A new play is scrobbled in its own right", "[scrobbler][reset]") {
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(scrobbler);

    const auto first = makeTrack("Bohemian Rhapsody");
    driver.reset();
    REQUIRE(pumpUntil(driver, first, [&] { return scrobbler->nowPlayings() >= 1; }));

    const auto second = makeTrack("Love of My Life");
    driver.reset();
    REQUIRE(pumpUntil(driver, second, [&] { return scrobbler->nowPlayings() >= 2; }));

    const auto playing = scrobbler->playing();
    REQUIRE(playing.size() >= 2);
    CHECK(playing[0] == first.identity);
    CHECK(playing[1] == second.identity);
}

TEST_CASE("A play ending mid-flight does not stall the next one", "[scrobbler][reset]") {
    // The result of the first now-playing lands after the user has already moved on. It must be
    // dropped, and must not leave the next play believing a call is still in flight.
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    scrobbler->latency = 80ms;

    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(scrobbler);

    const auto first = makeTrack("Bohemian Rhapsody");
    driver.reset();
    driver.tick(first);
    std::this_thread::sleep_for(10ms); // The call is with the worker, still unanswered.

    const auto second = makeTrack("Love of My Life");
    driver.reset();

    REQUIRE(pumpUntil(driver, second, [&] {
        const auto playing = scrobbler->playing();
        return std::ranges::any_of(playing, [&](const TrackIdentity &id) {
            return id == second.identity;
            });
        }));
}

TEST_CASE("A still-playing track has its now-playing refreshed", "[scrobbler][pause]") {
    // A now-playing update goes stale on the scrobbler's end; once the refresh interval passes and
    // the track is still playing, it is re-sent.
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    ScrobbleDriver driver(fastSchedule()); // Refresh interval 50ms.
    driver.registerScrobbler(scrobbler);

    const auto track = makeTrack();
    driver.reset();
    REQUIRE(pumpUntil(driver, track, [&] { return scrobbler->nowPlayings() >= 1; }));

    CHECK(pumpUntil(driver, track, [&] { return scrobbler->nowPlayings() >= 2; }));
}

TEST_CASE("A play that begins paused sends no now-playing until it resumes", "[scrobbler][pause]") {
    // The now-playing update means "listening now", so even the initial send is held while paused —
    // as happens when the user switches tracks without un-pausing. It goes out once playback starts.
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    ScrobbleDriver driver(fastSchedule());
    driver.registerScrobbler(scrobbler);

    auto track = makeTrack();
    track.status = Paused;
    driver.reset();

    pumpFor(driver, track, 150ms);
    CHECK(scrobbler->nowPlayings() == 0);

    // On resume the now-playing update is finally sent.
    track.status = Playing;
    CHECK(pumpUntil(driver, track, [&] { return scrobbler->nowPlayings() >= 1; }));
}

TEST_CASE("A refresh due while paused is held until the track resumes", "[scrobbler][pause]") {
    // The now-playing update means "listening now", so a refresh that falls due while paused is not
    // sent until playback resumes.
    auto scrobbler = std::make_shared<FakeScrobbler>("lastfm");
    ScrobbleDriver driver(fastSchedule()); // Refresh interval 50ms.
    driver.registerScrobbler(scrobbler);

    auto track = makeTrack();
    driver.reset();
    REQUIRE(pumpUntil(driver, track, [&] { return scrobbler->nowPlayings() >= 1; }));

    // Paused well past the refresh interval: the update is held back, not re-sent.
    track.status = Paused;
    pumpFor(driver, track, 200ms);
    CHECK(scrobbler->nowPlayings() == 1);

    // On resume it is finally re-sent.
    track.status = Playing;
    CHECK(pumpUntil(driver, track, [&] { return scrobbler->nowPlayings() >= 2; }));
}