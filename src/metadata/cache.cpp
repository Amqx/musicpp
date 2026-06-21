/**
 * @file cache.cpp
 * @author Jonathan Deng
 * @date 21-Jun-26
 */

#include "metadata/cache.hpp"
#include <filesystem>
#include "shlobj_core.h"
#include <leveldb/db.h>

MetadataCache::MetadataCache() {
    PWSTR path = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        throw std::exception("Failed to get LocalAppData folder");
    }

    const std::filesystem::path base_path(path);
    const std::filesystem::path db_path = base_path/"musicppv2"/"song_db";
    CoTaskMemFree(path);

    std::error_code ec;
    create_directories(db_path, ec);

    if (ec) {
        const std::string error = "Couldn't create db folder: " + ec.message();
        throw std::exception(error.c_str());
    }

    leveldb::DB *temp = nullptr;
    leveldb::Options options;
    options.create_if_missing = true;

    if (const leveldb::Status status = leveldb::DB::Open(options, db_path.string(), &temp); status.ok()) {
        _db.reset(temp);
    } else {
        const std::string error = "Couldn't initialize db: " + status.ToString();
        throw std::exception(error.c_str());
    }
}

MetadataCache::~MetadataCache() = default;

/**
 * Creates a database key for a given base track.
 * @param track Track's information.
 * @return Database key for the track.
 */
std::string getKey(const Track& track) {
    auto normalize = [] (std::string str) {
        std::ranges::replace(str, '|', '-');
        return str;
    };
    const auto title = normalize(track.identity.title);
    const auto artist = normalize(track.identity.artist);
    const auto album = normalize(track.identity.album);
    return title + artist + album;
}

/**
 * Creates the stored database value for an enriched track.
 * @param track Enriched track with a url.
 * @return Database value.
 */
std::string createValue(const EnrichedTrack& track) {
    std::string val;
    val.reserve(track.url->size() + 1);
    val.push_back(static_cast<char>(track.type));
    val += *track.url;
    return val;
}

/**
 * Attempts to parse a database output into an EnrichedTrack.
 * @param track Basic track information.
 * @param raw Raw database data.
 * @return Optional containing enriched track if parsing succeeded.
 */
std::optional<EnrichedTrack> parseValue(const Track& track, const std::string& raw) {
    if (raw.empty()) return std::nullopt;
    EnrichedTrack out;
    out.track = track;
    out.type = static_cast<ImageType>(static_cast<uint8_t>(raw[0]));
    out.url = raw.substr(1);
    return out;
}

void MetadataCache::writeEntry(const EnrichedTrack &track) const {
    if (!track.url) return;
    const auto key = getKey(track.track);
    const auto value = createValue(track);
    _db->Put(leveldb::WriteOptions(), key, value);
}

std::optional<EnrichedTrack> MetadataCache::findEntry(const Track &track) const {
    const auto key = getKey(track);
    std::string raw;
    if (const auto status = _db -> Get(leveldb::ReadOptions(), key, &raw); !status.ok()) {
        return std::nullopt;
    }
    return parseValue(track, raw);
}
