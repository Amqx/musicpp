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

TEST_CASE("A decorated title falls outside the gate", "[matching][known-gap]") {
    // Documents a limitation rather than an intention. The gate is an edit-distance ratio over the
    // whole string, so every character a source appends counts against the match: "(Remastered
    // 2011)" alone drags an otherwise perfect title down to ~49%, under kMatchGenerosity.
    //
    // Scraper works around this with a substring check alongside the fuzzy one (scraper.cpp:123).
    // LastFm has no such fallback (lastfm.cpp:334), so it discards these results outright.
    CHECK_FALSE(fuzzyMatch("Bohemian Rhapsody", "Bohemian Rhapsody (Remastered 2011)"));
    CHECK_FALSE(fuzzyMatch("Under Pressure", "Under Pressure - Remastered"));
}

TEST_CASE("Unrelated strings do not match", "[matching]") {
    CHECK_FALSE(fuzzyMatch("Bohemian Rhapsody", "Stairway to Heaven"));
    CHECK_FALSE(fuzzyMatch("Queen", "Led Zeppelin"));
    CHECK_FALSE(fuzzyMatch("Bohemian Rhapsody", ""));
}

TEST_CASE("A different track by the same name shape does not match", "[matching]") {
    // Just past the 60% gate is where the interesting failures live.
    CHECK_FALSE(fuzzyMatch("Love of My Life", "Life of My Love (Cover)"));
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
