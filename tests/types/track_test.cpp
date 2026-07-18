/**
 * @file track_test.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <sstream>
#include "types/track.hpp"

using namespace std::chrono_literals;

namespace {
/**
 * The instant TrackTiming::set() measures against: steady_clock ticks, as the poller feeds it.
 */
int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int64_t ns(const std::chrono::nanoseconds duration) { return duration.count(); }

/**
 * Timings are read against a running clock, so a position is only ever right to within the time
 * the test itself takes. A second of slack is far tighter than the values under test.
 */
constexpr auto kSlack = 1s;

bool near(const std::chrono::nanoseconds actual, const std::chrono::nanoseconds expected) {
    return actual >= expected - kSlack && actual <= expected + kSlack;
}

TrackIdentity makeIdentity(const std::string &title) {
    TrackIdentity identity;
    identity.title = title;
    identity.artist = "Queen";
    identity.album = "A Night at the Opera";
    return identity;
}
} // namespace

TEST_CASE("A track playing reports where it is", "[track][timing]") {
    // Started a minute ago, three minutes left to run.
    TrackTiming timing;
    timing.set(nowNs() - ns(60s), nowNs() + ns(180s));

    CHECK(near(timing.current(), 60s));
    CHECK(near(timing.remaining(), 180s));
    CHECK(near(timing.total(), 240s));
}

TEST_CASE("Total length does not depend on the current time", "[track][timing]") {
    // A track that finished playing an hour ago is still four minutes long.
    TrackTiming played;
    played.set(nowNs() - ns(64min), nowNs() - ns(60min));
    CHECK(near(played.total(), 4min));

    // So is one that has not started yet.
    TrackTiming queued;
    queued.set(nowNs() + ns(60min), nowNs() + ns(64min));
    CHECK(near(queued.total(), 4min));
}

TEST_CASE("A track that has not started yet is at its beginning", "[track][timing]") {
    // A position is never negative: a track queued but not started is simply at 0:00, with its
    // whole length still to run.
    TrackTiming timing;
    timing.set(nowNs() + ns(30s), nowNs() + ns(210s));

    CHECK(timing.current() == 0ns);
    CHECK(near(timing.remaining(), 180s));
    CHECK(near(timing.remaining(), timing.total()));
}

TEST_CASE("A track that has run out has nothing remaining", "[track][timing]") {
    // Likewise a remaining time never goes negative: a track past its end has 0:00 left.
    TrackTiming timing;
    timing.set(nowNs() - ns(300s), nowNs() - ns(60s));

    CHECK(timing.remaining() == 0ns);
    CHECK(near(timing.current(), 300s));
    CHECK(near(timing.total(), 240s));
}

TEST_CASE("The endpoints span the total length", "[track][timing]") {
    // start() and end() are wall-clock seconds; total() is a duration in nanoseconds.
    TrackTiming timing;
    timing.set(nowNs() - ns(10s), nowNs() + ns(230s));

    const auto span = std::chrono::seconds(timing.end() - timing.start());
    CHECK(near(span, timing.total()));
}

TEST_CASE("An unset timing reads as a zero-length track at its start", "[track][timing]") {
    const TrackTiming timing;

    CHECK(timing.current() == 0ns);
    CHECK(timing.total() == 0ns);
}

TEST_CASE (

"An unset timing has nothing remaining"
,
"[track][timing]"
)
 {
    // The {min, max} sentinel must not leak into remaining(): max - now would be a ~292-year span.
    const TrackTiming timing;

    CHECK(timing.remaining() == 0ns);
}

TEST_CASE (

"An unset timing's endpoints read as the epoch"
,
"[track][timing]"
)
 {
    // start()/end() convert the steady_clock endpoints to wall time; the {min, max} sentinel must
    // not reach that arithmetic (sentinel - now overflows int64). An unset timing reads as 0.
    const TrackTiming timing;

    CHECK(timing.start() == 0);
    CHECK(timing.end() == 0);
}

TEST_CASE (

"Unset timings compare equal"
,
"[track][timing][equality]"
)
 {
    // operator== compares start()/end(); two default-constructed timings must not overflow into
    // spuriously-unequal wall-clock values.
    const TrackTiming left;
    const TrackTiming right;

    CHECK(left == right);
}

TEST_CASE("Identities compare by every field", "[track][equality]") {
    const auto base = makeIdentity("Bohemian Rhapsody");

    CHECK(base == makeIdentity("Bohemian Rhapsody"));

    auto title = base;
    title.title = "Love of My Life";
    CHECK_FALSE(base == title);

    auto artist = base;
    artist.artist = "David Bowie";
    CHECK_FALSE(base == artist);

    auto album = base;
    album.album = "Jazz";
    CHECK_FALSE(base == album);
}

TEST_CASE("An identity equals its copy", "[track][equality]") {
    const auto identity = makeIdentity("Bohemian Rhapsody");
    const auto copy = identity; // NOLINT(*-unnecessary-copy-initialization)
    CHECK(identity == copy);
}

TEST_CASE("Timings set to the same instants are equal", "[track][equality]") {
    const auto start = nowNs();
    const auto end = start + ns(240s);

    TrackTiming left;
    left.set(start, end);
    TrackTiming right;
    right.set(start, end);

    CHECK(left == right);

    TrackTiming later;
    later.set(start + ns(10s), end);
    CHECK_FALSE(left == later);
}

TEST_CASE("Tracks compare by identity, timing and status", "[track][equality]") {
    Track base;
    base.identity = makeIdentity("Bohemian Rhapsody");
    base.timing.set(nowNs(), nowNs() + ns(240s));
    base.status = Playing;

    auto same = base;
    CHECK(base == same);

    auto renamed = base;
    renamed.identity.title = "Love of My Life";
    CHECK_FALSE(base == renamed);

    auto paused = base;
    paused.status = Paused;
    CHECK_FALSE(base == paused);
}

TEST_CASE("A default track is unknown and empty", "[track]") {
    const Track track;
    CHECK(track.status == Unknown);
    CHECK(track.identity.title.empty());
    CHECK(track.identity.artist.empty());
    CHECK(track.identity.album.empty());
}

TEST_CASE("Image types name themselves", "[track][image]") {
    CHECK(to_string(Static) == "Static");
    CHECK(to_string(Animated) == "Animated");
}

TEST_CASE("A default enriched track carries nothing", "[track][enriched]") {
    const EnrichedTrack enriched;
    CHECK(enriched.image.url.empty());
    CHECK(enriched.image.type == Static);
    CHECK(enriched.songUrls.empty());
    CHECK_FALSE(enriched.pause.since.has_value());
}

TEST_CASE("An identity streams its fields", "[track][stream]") {
    std::ostringstream out;
    out << makeIdentity("Bohemian Rhapsody");

    const auto text = out.str();
    CHECK(text.find("Bohemian Rhapsody") != std::string::npos);
    CHECK(text.find("Queen") != std::string::npos);
}
