/**
 * @file track.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#pragma once
#include <string>
#include <chrono>
#include <optional>

class TrackIdentity {
public:
    std::string title;
    std::string artist;
    std::string album;

    TrackIdentity();
};
std::ostream& operator<<(std::ostream& os, const TrackIdentity& track);
bool operator==(const TrackIdentity& l, const TrackIdentity& r);

class TrackTiming {
public:
    /**
     * Current position in track relative to the current time.
     * @return Current position in ns.
     */
    [[nodiscard]] std::chrono::nanoseconds current() const;

    /**
     * Remaining track length relative to the current time.
     * @return Remaining length in ns.
     */
    [[nodiscard]] std::chrono::nanoseconds remaining() const;

    /**
     * Total track length.
     * @return Track length in ns.
     */
    [[nodiscard]] std::chrono::nanoseconds total() const;

    TrackTiming();

    /**
     * Sets the timing parameters.
     * @param start_ Start of the track, since unix epoch, in ns.
     * @param end_ End of the track, since unix epoch, in ns.
     */
    void set(const int64_t& start_, const int64_t& end_);

    [[nodiscard]] int64_t start() const;
    [[nodiscard]] int64_t end() const;

private:
    std::chrono::time_point<std::chrono::steady_clock> _start;
    std::chrono::time_point<std::chrono::steady_clock> _end;
};
std::ostream& operator<<(std::ostream& os, const TrackTiming& timing);
bool operator==(const TrackTiming& l, const TrackTiming& r);

enum TrackStatus {
    Playing,
    Paused,
    Stopped,
    Unknown
};
std::ostream& operator<<(std::ostream& os, const TrackStatus& status);

class Track {
public:
    TrackIdentity identity;
    TrackTiming timing;
    TrackStatus status = Unknown;
};
std::ostream& operator<<(std::ostream& os, const Track& track);
bool operator==(const Track& l, const Track& r);


enum ImageType : std::uint8_t {
    Static = 0,
    Animated = 1
};

constexpr std::string to_string(const ImageType& type) {
    switch (type) {
        case Static: return "Static";
        case Animated: return "Animated";
        default: return "Unknown";
    }
}

class EnrichedTrack {
public:
    Track track;
    std::optional<std::string> url;
    ImageType type = Static;
};
