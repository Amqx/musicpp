/**
 * @file track.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#include "types/track.hpp"
#include <iostream>

std::ostream &operator<<(std::ostream &os, const TrackIdentity &track) {
    os << track.title << " by " << track.artist << " on " << track.album;
    return os;
}

bool operator==(const TrackIdentity &l, const TrackIdentity &r) {
    return l.title == r.title && l.artist == r.artist && l.album == r.album;
}

std::chrono::nanoseconds TrackTiming::current() const {
    const auto now = std::chrono::steady_clock::now();
    if (_start > now || _start.time_since_epoch().count() == 0) {
        return std::chrono::nanoseconds::zero();
    }
    return now - _start;
}

TrackIdentity::TrackIdentity() {
    artist = "";
    album = "";
    title = "";
}

TrackTiming::TrackTiming() {
    _start = std::chrono::time_point<std::chrono::steady_clock>::min();
    _end = std::chrono::time_point<std::chrono::steady_clock>::max();
}

namespace {
    std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds> steadyToSystem(
        const std::chrono::time_point<std::chrono::steady_clock> &tp) {
    const auto steady_now = std::chrono::steady_clock::now();
    const auto system_now = std::chrono::system_clock::now();
    return system_now + (tp - steady_now);
}

}

int64_t TrackTiming::start() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
        steadyToSystem(_start).time_since_epoch()
    ).count();
}

int64_t TrackTiming::end() const {
    return std::chrono::duration_cast<std::chrono::seconds>(
        steadyToSystem(_end).time_since_epoch()
    ).count();
}

void TrackTiming::set(const int64_t &start_, const int64_t &end_) {
    _start = std::chrono::time_point<std::chrono::steady_clock>(std::chrono::nanoseconds(start_));
    _end = std::chrono::time_point<std::chrono::steady_clock>(std::chrono::nanoseconds(end_));
}

std::chrono::nanoseconds TrackTiming::remaining() const {
    const auto now = std::chrono::steady_clock::now();
    if (now >= _end) {
        return std::chrono::nanoseconds::zero();
    }
    if (now < _start) {
        return total();
    }
    return _end - now;
}

std::chrono::nanoseconds TrackTiming::total() const {
    if (_end < _start)
        return std::chrono::nanoseconds(0);
    return _end - _start;
}

std::ostream &operator<<(std::ostream &os, const TrackTiming &timing) {
    auto fmt = [](const std::chrono::nanoseconds &ns) {
        const auto s = duration_cast<std::chrono::seconds>(ns).count();
        return std::format("{:02d}:{:02d}:{:02d}", s / 3600, (s % 3600) / 60, s % 60);
    };
    auto fmtUnix = [](const int64_t &unix_s) {
        const std::chrono::system_clock::time_point tp{std::chrono::seconds(unix_s)};
        const std::time_t t = std::chrono::system_clock::to_time_t(tp);
        std::tm local{};
        localtime_s(&local, &t);
        char tz[64]{};
        strftime(tz, sizeof(tz), "%Z", &local);
        return std::format("{:02d}:{:02d}:{:02d} {}",
                           local.tm_hour, local.tm_min, local.tm_sec,
                           tz);
    };
    os << fmtUnix(timing.start()) << " - " << fmtUnix(timing.end()) << " | " <<
            fmt(timing.current()) << " elapsed, "
            << fmt(timing.remaining()) << " remaining, "
            << fmt(timing.total()) << " total";
    return os;
}

bool operator==(const TrackTiming &l, const TrackTiming &r) {
    constexpr auto threshold = std::chrono::milliseconds(500);
    const auto diff_start = std::chrono::nanoseconds(std::abs(l.start() - r.start()));
    const auto diff_end = std::chrono::nanoseconds(std::abs(l.end() - r.end()));
    return diff_start <= threshold && diff_end <= threshold;
}

std::ostream &operator<<(std::ostream &os, const TrackStatus &status) {
    switch (status) {
        case Playing:
            os << "Playing";
            break;
        case Paused:
            os << "Paused";
            break;
        case Stopped:
            os << "Stopped";
            break;
        case Unknown:
            os << "Unknown";
            break;
    }
    return os;
}

std::ostream &operator<<(std::ostream &os, const Track &track) {
    os << "Track { identity: " << track.identity << ", timing: " << track.timing << ", status: " <<
            track.status << " }";
    return os;
}

bool operator==(const Track &l, const Track &r) {
    return l.identity == r.identity && l.timing == r.timing && l.status == r.status;
}
