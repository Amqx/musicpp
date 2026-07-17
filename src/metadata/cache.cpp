/**
 * @file cache.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#include "metadata/cache.hpp"
#include "metadata/cache_codec.hpp"
#include <filesystem>
#include "system/paths.hpp"
#include <leveldb/db.h>
#include <leveldb/write_batch.h>

namespace {
/**
 * Resolves the default cache directory under LocalAppData.
 */
std::filesystem::path defaultDbPath() {
    return paths::appDataDir() / "song_db";
}

}

using namespace cache_codec;

MetadataCache::MetadataCache() {
    open(defaultDbPath());
}

MetadataCache::MetadataCache(const std::filesystem::path &dbPath) {
    open(dbPath);
}

void MetadataCache::open(const std::filesystem::path &dbPath) {
    std::error_code ec;
    create_directories(dbPath, ec);
    if (ec) {
        const std::string error = "Couldn't create db folder: " + ec.message();
        throw std::exception(error.c_str());
    }

    leveldb::DB *temp = nullptr;
    leveldb::Options options;
    options.create_if_missing = true;

    if (const leveldb::Status status = leveldb::DB::Open(options, dbPath.string(), &temp); status.
        ok()) {
        _db.reset(temp);
        sweepExpired();
    } else {
        const std::string error = "Couldn't initialize db: " + status.ToString();
        throw std::exception(error.c_str());
    }
}

void MetadataCache::sweepExpired() const {
    const leveldb::Slice prefix("img|");
    const auto now = nowSeconds();

    leveldb::WriteBatch batch;
    bool hasDelete = false;

    // Image keys sort before url keys, so seeking to the prefix and stopping once it no longer
    // matches walks the image rows alone.
    const std::unique_ptr<leveldb::Iterator> it(_db->NewIterator(leveldb::ReadOptions()));
    for (it->Seek(prefix); it->Valid() && it->key().starts_with(prefix); it->Next()) {
        // A malformed row is unusable to findEntry anyway, so the sweep is also how it leaves.
        if (const auto cached = parseImageValue(it->value().ToString());
            !cached || !isFresh(cached->written_at, now)) {
            batch.Delete(it->key());
            hasDelete = true;
        }
    }

    if (hasDelete) {
        _db->Write(leveldb::WriteOptions(), &batch);
    }
}

MetadataCache::~MetadataCache() = default;

void MetadataCache::writeEntry(const EnrichedTrack &track) const {
    leveldb::WriteBatch batch;
    bool hasWrite = false;

    if (!track.image.url.empty()) {
        batch.Put(imageKey(track.track), createImageValue(track.image, nowSeconds()));
        hasWrite = true;
    }

    if (!track.songUrls.empty()) {
        std::vector<SongUrl> existing;
        if (std::string rawExisting; _db->Get(leveldb::ReadOptions(), urlKey(track.track),
                                              &rawExisting).ok()) {
            existing = parseUrlValue(rawExisting);
        }
        if (const auto merged = mergeSongUrls(std::move(existing), track.songUrls); !merged.
            empty()) {
            batch.Put(urlKey(track.track), createUrlValue(merged));
            hasWrite = true;
        }
    }

    if (hasWrite) {
        _db->Write(leveldb::WriteOptions(), &batch);
    }
}

std::optional<EnrichedTrack> MetadataCache::findEntry(const Track &track) const {
    std::string rawImage;
    std::string rawUrls;
    const bool hasImage = _db->Get(leveldb::ReadOptions(), imageKey(track), &rawImage).ok();
    const bool hasUrls = _db->Get(leveldb::ReadOptions(), urlKey(track), &rawUrls).ok();

    // An image is usable only if it parses and is still within its TTL. An expired image is
    // withheld as though it were absent to force a refresh.
    std::optional<ImageUrl> image;
    if (hasImage) {
        if (const auto cached = parseImageValue(rawImage);
            cached && isFresh(cached->written_at, nowSeconds())) {
            image = cached->image;
        }
    }

    if (!image && !hasUrls)
        return std::nullopt;

    EnrichedTrack out;
    out.track = track;
    if (image) {
        out.image = *image;
    }
    if (hasUrls) {
        out.songUrls = parseUrlValue(rawUrls);
    }
    return out;
}