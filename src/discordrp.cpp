//
// Created by Jonathan on 26-Sep-25.
//

#include <discordrp.h>
#include <iostream>

discordrp::discordrp(mediaPlayer *player, const uint64_t apikey) {
    this->appleMusic = player;
    this->clientID = apikey;
    running = true;
    this->refreshThread = thread(&discordrp::refreshLoop, this);
    client->SetApplicationId(clientID);
    client->Connect();
}

void discordrp::refreshLoop() const {
    while (running) {
        discordpp::RunCallbacks();
        this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

string discordrp::convertWString(const wstring &wstr) {
    if (wstr.empty()) {
        return "";
    }

    int required_size = WideCharToMultiByte(
        CP_UTF8, // CodePage: Convert to UTF-8
        0, // dwFlags
        wstr.data(), // lpWideCharStr: Pointer to wide string data
        static_cast<int>(wstr.length()), // cchWideChar: Length of the wide string (excluding null)
        nullptr, // lpMultiByteStr: Output buffer (NULL to get size)
        0, // cbMultiByte: Output buffer size (0 to get size)
        nullptr, nullptr // lpDefaultChar, lpUsedDefaultChar
    );

    if (required_size <= 0) {
        return ""; // Conversion error
    }
    std::string narrow_str(required_size, '\0');

    WideCharToMultiByte(
        CP_UTF8, // CodePage: Convert to UTF-8
        0, // dwFlags
        wstr.data(), // lpWideCharStr
        static_cast<int>(wstr.length()), // cchWideChar
        &narrow_str[0], // lpMultiByteStr: Use the internal buffer (C++11+)
        required_size, // cbMultiByte: Size of the buffer
        nullptr, nullptr
    );

    return narrow_str;
}

discordrp::~discordrp() {
    running = false;
    client->Disconnect();
    if (refreshThread.joinable()) refreshThread.join();
}

void discordrp::update() const {
    if (!appleMusic->getTitle().empty() && !appleMusic->getArtist().empty() && !appleMusic->getAlbum().empty()) {
        discordpp::Activity activity;
        activity.SetType(discordpp::ActivityTypes::Listening);
        activity.SetName(convertWString(appleMusic->getArtist()));
        activity.SetDetails(convertWString(appleMusic->getTitle())); // title
        activity.SetState(convertWString(appleMusic->getArtist())); // artist

        // enable listening to:
        activity.SetStatusDisplayType(discordpp::StatusDisplayTypes::State);

        if (appleMusic->getState()) {
            discordpp::ActivityTimestamps timestamps;
            timestamps.SetStart(appleMusic->getStartTS());
            timestamps.SetEnd(static_cast<uint64_t>(appleMusic->getEndTS()));
            activity.SetTimestamps(timestamps);

            discordpp::ActivityAssets assets;
            assets.SetLargeImage(convertWString(appleMusic->getImage()));
            assets.SetLargeText(convertWString(appleMusic->getAlbum()));
            activity.SetAssets(assets);
        } else {
            discordpp::ActivityTimestamps timestamps;
            timestamps.SetStart(appleMusic->getPauseTimer());
            activity.SetTimestamps(timestamps);

            discordpp::ActivityAssets assets;
            assets.SetLargeImage(convertWString(appleMusic->getImage()));
            assets.SetLargeText(convertWString(appleMusic->getAlbum()));
            assets.SetSmallImage("pause");
            assets.SetSmallText("Paused");
            activity.SetAssets(assets);
        }

        client->UpdateRichPresence(activity, [](const discordpp::ClientResult &result) {
            if (!result.Successful()) {
                cerr << "Discord RPC update failed (" << static_cast<int>(result.Type())
                        << "): " << result.Error() << endl;
            }
        });
    } else {
        client->ClearRichPresence();
    }
}
