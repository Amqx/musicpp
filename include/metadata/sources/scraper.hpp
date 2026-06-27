/**
 * @file scraper.hpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#pragma once

#include <string>
#include "source.hpp"
#include "types/results.hpp"

class Scraper : public MetadataWebSource {
public:
    explicit Scraper(const std::string& region);

    std::string identify() override;

    SearchResult searchTrack(const Track &track) override;

private:
    std::string _region;
    const std::string kIDENTITY = "Apple Music Web Scraper";
};
