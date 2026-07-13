/**
 * @file source.hpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#pragma once
#include "types/results.hpp"
#include "types/track.hpp"

class MetadataWebSource {
public:
    virtual ~MetadataWebSource() = default;

    [[nodiscard]] virtual SearchResult searchTrack(const Track &track) = 0;

    [[nodiscard]] virtual std::string identify() = 0;
};