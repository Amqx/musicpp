/**
 * @file cache.hpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#pragma once
#include <leveldb/db.h>
#include "types/track.hpp"

class MetadataCache {
public:
    explicit MetadataCache();
    ~MetadataCache();

    MetadataCache(const MetadataCache&) = delete;
    MetadataCache& operator=(const MetadataCache&) = delete;

    /**
     * Writes an entry to the database cache.
     * @param track An enriched track with an image url.
     */
    void writeEntry(const EnrichedTrack& track) const;

    /**
     * Attempts to find a given track within the database cache.
     * @param track A base track to find an url for.
     * @return An optional containing an EnrichedTrack if an image is found.
     */
    std::optional<EnrichedTrack> findEntry(const Track& track) const;

private:
    std::unique_ptr<leveldb::DB> _db;
};
