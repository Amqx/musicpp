# MusicPP V2

Discord Rich Presence for the Windows Apple Music desktop app written in C++/WinRT.

## About

V2 of MusicPP aims to rewrite the entire program into a more structured, better managed project, making it easier to add
features. V1 had multiple god classes in thousand-line files, making it hard to navigate. V2 has yet to reach
feature-parity with V1.

## Changes from V1

- Spotify is no longer available as an image source as its API has been locked behind their paid subscriptions.
  Furthermore, analyzing my own logs revealed that Spotify failed to match more than 95% of the time.
- I am not sure if I will port the animated images feature. As cool as it is, I feel it is rarely used and adds a lot of
  complexity with FFmpeg.
- There is no migration from V1. If you want to fully remove V1 from your computer, delete the following directory:
  `%LOCALAPPDATA%\\musicpp`.

## Missing from V2

- Setup code
- Apple Music region selection
- Apple Music animated images (likely not being ported)

## Roadmap

- Actual Win UI3 configuration menu
- Allow configuration of the Discord rich presence
- Allow changing keys/ re-authentication without program restart

## Requirements

- Windows 10/11 with the Apple Music app installed
- Optional API credentials:
    - Imgur Client ID ([Available Here](https://api.imgur.com/oauth2/addclient))
    - Last.fm API Key + Secret ([Available Here](https://www.last.fm/api/account/create))

Saved data locations:

- Cache/DB: `%LOCALAPPDATA%\\musicppv2\\song_db`
- Logs: `%LOCALAPPDATA%\\musicppv2\\logs`
