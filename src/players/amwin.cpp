/**
 * @file amwin.cpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

#include "players/amwin.hpp"
#include <winrt/Windows.Media.Control.h>
#include <winrt/windows.foundation.collections.h>
#include <winrt/Windows.Storage.Streams.h>
#include "system/winrt.hpp"

namespace {

const std::wstring kWindowsIdentifier = L"AppleInc.AppleMusicWin_nzyj5cx40ttqa!App";

/**
 * Attempts to find a running Apple Music Windows session using its source app user model id.
 * @param sessions
 * @return A valid session if found, nullptr otherwise.
 */
winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession findSession(
    const winrt::Windows::Foundation::Collections::IVectorView<
        winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession> &sessions) {
    for (const auto &s: sessions) {
        if (s.SourceAppUserModelId() == kWindowsIdentifier) {
            return s;
        }
    }
    return nullptr;
}

/**
 * Attempts to split Apple Music's conjoined artist and album info.
 * @param str Conjoined artist and album string.
 * @return Tuple containing artist, then album. If the split fails, both will be equivalent to the input.
 */
std::tuple<std::string, std::string> splitArtistAlbum(const winrt::hstring &str) {
    const std::wstring_view s{str};
    const auto sep = s.find(L'\u2014');
    if (sep == std::wstring::npos) {
        const auto r = winrt::to_string(s);
        return std::tuple{r, r};
    }
    const auto artistEnd = sep > 0 ? sep - 1 : 0;
    const auto albumStart = sep + 2 < s.size() ? sep + 2 : s.size();
    const auto artist = winrt::to_string(s.substr(0, artistEnd));
    const auto album = winrt::to_string(s.substr(albumStart));
    return std::tuple{artist, album};
}

/**
 * Parses timeline info from Windows properties.
 * @param timeline Timeline properties object.
 * @return Tuple containing start in ns since unix epoch and end in ns since unix epoch.
 */
std::tuple<int64_t, int64_t> parseTimeline(
    const
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionTimelineProperties &
    timeline) {
    const auto positionNs = timeline.Position().count() * 100;
    const auto endNs = timeline.EndTime().count() * 100;

    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto start = now - positionNs;
    const auto end = start + endNs;

    return std::tuple{start, end};
}

/**
 * Matches a Windows playback status to custom TrackStatus.
 * @param status Windows playback status.
 * @return Matched TrackStatus.
 */
TrackStatus parseStatus(
    const winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus &
    status) {
    switch (status) {
        case
        winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped
        :
            return Stopped;
        case
        winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing
        :
            return Playing;
        case
        winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
            return Paused;
        default:
            return Unknown;
    }
}

/**
 * Parses the raw thumbnail from windows into a vector of bytes.
 * @param streamRef Stream reference from Windows.
 * @return Vector of bytes containing image data.
 */
std::vector<unsigned char> parseThumbnail(
    const winrt::Windows::Storage::Streams::IRandomAccessStreamReference &streamRef) {
    if (!streamRef)
        return {};

    const winrt::Windows::Storage::Streams::IRandomAccessStreamWithContentType stream = streamRef.
            OpenReadAsync().get();
    const auto size = static_cast<uint32_t>(stream.Size());
    const winrt::Windows::Storage::Streams::Buffer buf(size);
    std::ignore = stream.ReadAsync(buf, size,
                                   winrt::Windows::Storage::Streams::InputStreamOptions::None).
            get();
    unsigned char *data = buf.data();
    return {data, data + buf.Length()};
}

}

AmWin::AmWin() {
    WinRtInit::initialize();
}

std::string AmWin::identify() {
    return kIdentifier;
}

std::tuple<Track, std::optional<std::vector<unsigned char> > > AmWin::poll() {
    const auto manager =
            winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync()
            .get();
    if (!manager)
        return {Track(), std::nullopt};

    const auto sessions = manager.GetSessions();
    const auto am = findSession(sessions);
    if (!am)
        return {Track(), std::nullopt};

    const auto properties = am.TryGetMediaPropertiesAsync().get();
    const auto timeline = am.GetTimelineProperties();
    const auto rawStatus = am.GetPlaybackInfo();
    const auto rawImg = properties.Thumbnail();

    Track t;
    t.identity.title = winrt::to_string(properties.Title());
    if (t.identity.title.ends_with("'s Station"))
        return {};

    const auto [artist, album] = splitArtistAlbum(properties.Artist());
    t.identity.artist = artist;
    t.identity.album = album;

    const auto [start, end] = parseTimeline(timeline);
    t.timing.set(start, end);

    const auto status = parseStatus(rawStatus.PlaybackStatus());
    t.status = status;

    const auto thumbnailBytes = parseThumbnail(rawImg);

    return {t, thumbnailBytes};
}
