/**
 * @file cache.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#pragma once
#include <optional>
#include <filesystem>
#include <leveldb/db.h>
#include "types/track.hpp"

class MetadataCache {
public:
    explicit MetadataCache();

    /**
     * Opens the cache at an explicit database path.
     * @param dbPath Directory the leveldb database lives in.
     */
    explicit MetadataCache(const std::filesystem::path &dbPath);

    ~MetadataCache();

    MetadataCache(const MetadataCache &) = delete;

    MetadataCache &operator=(const MetadataCache &) = delete;

    /**
     * Writes an entry to the database cache.
     * @param track An enriched track with an image url.
     */
    void writeEntry(const EnrichedTrack &track) const;

    /**
     * Attempts to find a given track within the database cache.
     * @param track A base track to find an url for.
     * @return An optional containing an EnrichedTrack if an image is found.
     */
    [[nodiscard]] std::optional<EnrichedTrack> findEntry(const Track &track) const;

private:
    void open(const std::filesystem::path &dbPath);

    std::unique_ptr<leveldb::DB> _db;
};
