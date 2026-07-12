/**
 * @file enricher.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#include "metadata/enricher.hpp"

#include <set>
#include <string>
#include <utility>

Enricher::Enricher(MetadataCache &cache) : _cache(cache) {
}

void Enricher::registerSource(std::shared_ptr<MetadataWebSource> source) {
    _sources.push_back(std::move(source));
}

void Enricher::registerUploader(std::unique_ptr<Uploader> uploader) {
    _uploaders.push_back(std::move(uploader));
}

EnrichedTrack Enricher::enrich(const Track &track,
                               const std::optional<std::vector<unsigned char> > &thumbnail) const {
    EnrichedTrack out;
    out.track = track;

    // Seed from the cache so we only fetch what's missing.
    if (const auto cached = _cache.findEntry(track)) {
        out.image = cached->image;
        out.songUrls = cached->songUrls;
    }

    bool needImage = out.image.url.empty();
    std::set<std::string> ownedPlatforms;
    for (const auto &[url, source] : out.songUrls) {
        ownedPlatforms.insert(source);
    }

    bool changed = false;

    // Check sources first
    for (const auto &source : _sources) {
        const std::string platform = source->identify();
        const bool needLink = !ownedPlatforms.contains(platform);
        if (!needImage && !needLink) {
            continue; // nothing to gain from this source
        }

        const auto [image_url, web_url, image_type] = source->searchTrack(track);

        if (needImage && !image_url.empty()) {
            out.image = ImageUrl{image_url, image_type, platform};
            needImage = false;
            changed = true;
        }
        if (needLink && !web_url.empty()) {
            out.songUrls.push_back(SongUrl{web_url, platform});
            ownedPlatforms.insert(platform);
            changed = true;
        }
    }

    // Only upload if we still don't possess an image
    if (needImage && thumbnail.has_value()) {
        for (const auto &uploader : _uploaders) {
            if (const auto [image_url] = uploader->uploadImage(*thumbnail, Static); !image_url.
                empty()) {
                out.image = ImageUrl{image_url, Static, uploader->identify()};
                changed = true;
                break;
            }
        }
    }

    if (changed) {
        _cache.writeEntry(out);
    }
    return out;
}
