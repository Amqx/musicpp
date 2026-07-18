/**
 * @file matching_test.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 12-Jul-26
 */

#include <catch2/catch_test_macros.hpp>
#include <string>
#include "metadata/matching.hpp"

TEST_CASE("Identical strings match", "[matching]") {
    CHECK(fuzzyMatch("Bohemian Rhapsody", "Bohemian Rhapsody"));
    CHECK(fuzzyMatch("a", "a"));
}

TEST_CASE("Matching ignores case", "[matching]") {
    CHECK(fuzzyMatch("Bohemian Rhapsody", "bohemian rhapsody"));
    CHECK(fuzzyMatch("QUEEN", "queen"));
}

TEST_CASE("Small differences in spelling still match", "[matching]") {
    // What a web source hands back is rarely spelled exactly like what the player reports.
    CHECK(fuzzyMatch("Don't Stop Me Now", "Don’t Stop Me Now"));
    CHECK(fuzzyMatch("Bohemian Rhapsody", "Bohemian Rapsody"));
    CHECK(fuzzyMatch("Queen", "Queen "));
}

TEST_CASE (

"A decorated title still matches"
,
"[matching]"
)
 {
    // A source routinely appends decoration a player omits ("(Remastered 2011)"). An edit-distance
    // ratio over the whole string counts every appended character against the match, dragging an
    // otherwise perfect title under kMatchGenerosity. With the substring fallback opted in (floored
    // so a short needle can't match incidentally), containment matches regardless.
    CHECK(fuzzyMatch("Bohemian Rhapsody", "Bohemian Rhapsody (Remastered 2011)", true));
    CHECK(fuzzyMatch("Under Pressure", "Under Pressure - Remastered", true));
}

TEST_CASE (

"The substring fallback is opt-in"
,
"[matching]"
)
 {
    // The fallback is a title-only concession: on an artist it invites false positives, so a plain
    // fuzzyMatch (substring off) holds the ratio and rejects containment the same way the old code
    // did, while a caller that knows it is matching a title can opt back in.
    CHECK_FALSE(fuzzyMatch("Radiohead", "Radiohead Tribute"));
    CHECK_FALSE(fuzzyMatch("Queen", "Queen Naija"));
    CHECK(fuzzyMatch("Radiohead", "Radiohead Tribute", true));
}

TEST_CASE (

"A short needle does not match incidentally"
,
"[matching]"
)
 {
    // The substring fallback has a length floor: a needle too short to be distinctive must not
    // pull in an unrelated, much longer string just because it happens to appear inside it — even
    // when the fallback is opted in.
    CHECK_FALSE(fuzzyMatch("Go", "Good Riddance (Time of Your Life)", true));
}

TEST_CASE("Unrelated strings do not match", "[matching]") {
    CHECK_FALSE(fuzzyMatch("Bohemian Rhapsody", "Stairway to Heaven"));
    CHECK_FALSE(fuzzyMatch("Queen", "Led Zeppelin"));
    CHECK_FALSE(fuzzyMatch("Bohemian Rhapsody", ""));
}

TEST_CASE("A different track by the same name shape does not match", "[matching]") {
    // Just past the 60% gate is where the interesting failures live. Neither contains the other, so
    // it stays rejected even with the substring fallback opted in.
    CHECK_FALSE(fuzzyMatch("Love of My Life", "Life of My Love (Cover)", true));
}

TEST_CASE("Matching is symmetric", "[matching]") {
    const std::string polled = "Bohemian Rhapsody";
    const std::string found = "Bohemian Rhapsody (Remastered 2011)";

    CHECK(fuzzyMatch(polled, found) == fuzzyMatch(found, polled));
    CHECK(fuzzyMatch("Queen", "Led Zeppelin") == fuzzyMatch("Led Zeppelin", "Queen"));
    CHECK(fuzzyMatch("", "Queen") == fuzzyMatch("Queen", ""));
}

TEST_CASE("Two empty strings match", "[matching]") {
    // Tracks routinely arrive with an empty album; two of them are as similar as strings get.
    CHECK(fuzzyMatch("", ""));
}
