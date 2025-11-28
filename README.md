# MusicPP

Discord Rich Presence for Apple Music written in C++ using Windows Runtime APIs.

![full](images/full.png)

![status](images/mini.png)

## Features

- Displays track title, artist, and album
- Shows album art sourced from Apple Music (99% find rate), with Spotify and Imgur as fallbacks
- Caching with [LevelDB](https://github.com/google/leveldb)
- Progress bar/ timestamps
- Last.fm now playing and scrobbling support
- Tray icon for current status
- Support for "Listening to {title}" status (similar to old Spotify presence behaviour)
- Buttons to open the track in Apple Music and LastFM/ Spotify (when found)
- Low resource usage (~5MB RAM, ~0.1% CPU, ~11MB disk space (including dependencies))
- Written entirely in Windows native C++

Warning: this code does not have any configurability. If you want to modify the status display, you will have to do so
manually.
See below for more details.

## Requirements

- Microsot VC Redist ([lastest x64](https://aka.ms/vc14/vc_redist.x64.exe))
- Spotify API Credentials ([optional](https://developer.spotify.com/dashboard))
- Imgur API Credentials ([optional](https://api.imgur.com/oauth2/addclient))
- Last.fm API credentials ([options](https://www.last.fm/api/account/create))

## Configuring the rich presence

To modify the presence, do note that you will need to recompile the code. Please read the following section on how to
compile before continuing.

The code that updates the rich presence can be found [here](src/discordrp.cpp), in the`void discordrp::update()` method.
The available metadata has been listed below, and are already extracted for you. You can use Discord's
guide [here](https://discord.com/developers/docs/social-sdk/classdiscordpp_1_1Activity.html)
for more details.

#### Available metadata

For the source links, please check if they are not empty before using them (.empty). Furthermore, please use the current
playback
state to determine which timestamps should be used (start_ts and end_ts when playing is true, pause_ts when playing is
false).

The following are listed in the format: {name} ({type})({var name})

- Title (string)(title)
- Artist (string)(artist)
- Album (string)(album)
- Image link (string)(imglink)
- Apple Music link (string)(amlink)
- LastFM link (string)(LFMlink)
- Spotify link (string)(splink)
- Current playback state (boolean)(playing)
- Playback start time (int)(start_ts)
- Projected playback end time (int)(end_ts)
- When the current track was paused (int)(pause_ts)

## Building from Source

### CLion

The easiest way to do this is with CLion. Make sure you have the VCPKG extension installed.

1. Install [CLion](https://www.jetbrains.com/clion/). Make sure you have Microsoft Visual Studio installed.
    ```
    winget install --id=Microsoft.VisualStudio.2022.Community
   ```
2. Clone the project with git or the gui
    ```
    git clone https://github.com/Amqx/musicpp
   ```
3. Install the VCPKG extension and install the dependencies. You can use the GUI or call it manually. Make sure it is
   enabled for the project (VCPKG -> edit -> Add Vcpkg integration to existing CMake Profiles)
    - CURL
    - LevelDB
    - CPPWinRT
    - nlohmann/json
    - spdlog
    - LibXml2
    ```
    ~/.vcpkg-clion/vcpkg/vcpkg.exe install curl leveldb cppwinrt nlohmann-json spdlog libxml2
    ```
4. Install Discord Social SDK

   See below for more details.
5. Build (Shift + F10) and run.

### Manual:

1. Make sure you have CMake, Ninja and Microsoft Visual Studio installed.
    ```
   winget install --id=Kitware.CMake 
   winget install --id=Ninja-build.Ninja
   winget install --id=Microsoft.VisualStudio.2022.Community
   ```
2. Clone the repository: 
    ```
    git clone https://github.com/Amqx/musicpp
   ```
3. Get dependencies (VCPKG is probably easiest):
    ```
    vcpkg install curl leveldb cppwinrt nlohmann-json spdlog libxml2
    ```
   - CURL
   - LevelDB
   - CPPWinRT
   - nlohmann/json
   - spdlog
   - LibXml2
   
4. Install Discord Social SDK

   See below for more details.
   
5. Configure and build the project with CMake
    ```
    mkdir build
    cd build
    cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release  -DCMAKE_MAKE_PROGRAM={path to ninja} -DCMAKE_TOOLCHAIN_FILE={path to vcpkg}
    cmake --build . --target musicpp
    ```

## Installing the Discord Social SDK

You can download it from the [Discord Developer Portal](https://discord.com/developers/docs/game-sdk/sdk-starter-guide).
Do note that you have to login/ signup
to download the SDK. When prompted to create an application, do note that if you intend to
use your own application id, the name is what shows on Discord ("Listening to {name}" in the screenshot with the
buttons). If
you are going to use the default key, you can input whatever for the name. The section after (tell us a bit about your
game) you can write anything you want. Once
you have access to the downloads, make sure you get the one without Unity or Unreal in the name. The downloaded SDK
should be something like *DiscordSocialSdk-{version}.zip*. Extract it, then copy the folder discord_social_sdk into the
MusicPP
project root. Rename it to discordsdk so CMake can find it.