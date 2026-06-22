/**
 * @file enricher.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#pragma once
#include <memory>
#include <optional>
#include <vector>

#include "types/track.hpp"
#include "metadata/cache.hpp"
#include "metadata/sources/source.hpp"
#include "metadata/uploaders/uploader.hpp"

class Enricher {
public:
    /**
     * @param cache Long-lived metadata cache (not owned).
     * @param sources Web sources tried in order for image + song urls.
     * @param uploaders Uploaders tried in order to rehost thumbnail bytes.
     */
    Enricher(MetadataCache &cache,
             std::vector<std::unique_ptr<MetadataWebSource> > sources,
             std::vector<std::unique_ptr<Uploader> > uploaders);

    /**
     * Enriches a track with an image url and per-platform song urls.
     * @param track Base track to enrich.
     * @param thumbnail Optional raw thumbnail bytes from the poller.
     * @return A fully populated EnrichedTrack (image may be empty on total failure).
     */
    EnrichedTrack enrich(const Track &track,
                         const std::optional<std::vector<unsigned char> > &thumbnail) const;

private:
    MetadataCache &_cache;
    std::vector<std::unique_ptr<MetadataWebSource> > _sources;
    std::vector<std::unique_ptr<Uploader> > _uploaders;
};
