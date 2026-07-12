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
     */
    explicit Enricher(MetadataCache &cache);

    /**
     * Registers a web source tried during enrichment for image + song urls.
     * Sources are tried in registration order.
     * @param source Shared handle to the source.
     */
    void registerSource(std::shared_ptr<MetadataWebSource> source);

    /**
     * Registers an uploader tried during enrichment to rehost thumbnail bytes.
     * Uploaders are tried in registration order.
     * @param uploader Owning handle to the uploader.
     */
    void registerUploader(std::unique_ptr<Uploader> uploader);

    /**
     * Enriches a track with an image url and per-platform song urls.
     * @param track Base track to enrich.
     * @param thumbnail Optional raw thumbnail bytes from the poller.
     * @return A fully populated EnrichedTrack (image may be empty on total failure).
     */
    [[nodiscard]] EnrichedTrack enrich(const Track &track,
                                       const std::optional<std::vector<unsigned char> > &thumbnail) const;

private:
    MetadataCache &_cache;
    std::vector<std::shared_ptr<MetadataWebSource> > _sources{};
    std::vector<std::unique_ptr<Uploader> > _uploaders{};
};
