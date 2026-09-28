# BrewTube


# WARNING: THIS IS STILL IN EARLY DEVELOPMENT


A Homebrew YouTube App for the Wii based on [WiiMC](https://github.com/dborth/wiimc/) and [libwiigui](https://github.com/dborth/libgui) and inspired by [FourthTube](https://github.com/erievs/FourthTube)

## Installing
Download the [latest release](https://github.com/ReviveMii/brewtube/releases) or build the project

### Building the project
Install [Devkit-Pro](https://devkitpro.org/), [libogc2](https://github.com/extremscorner/libogc2) and [wii-curl](https://github.com/AndrewPiroli/wii-curl)

Run ```make```

## Features
- Search
- Video Playback
- Channels
- Captions
- Video Metadata
- No Ads
- Video Suggestions
- ReturnYouTubeDislikes
- Better Video Playback


## TODO
- Better Design
- ~~480p Video Streams with VISIONOS client with ANDROID fallback (current client, 360p is the only available stream in ANDROID) (requires implementing adaptive streams): [yt-dlp example](https://github.com/yt-dlp/yt-dlp/blob/c7fb478d21e9e59524befbe23f7801bb267fb880/yt_dlp/extractor/youtube/_base.py#L296-L310)~~
- Make VISIONOS client faster to replace ANDROID
- ~~Channels (channel name clickable in player and channels in search results)~~
- ~~Display video metadata and channel profile picture in player~~
- ~~Playlists (in search results)~~
- Sign-In (with What to Watch, Subscriptions, Watch Later, Watch History, Favorites, Uploads and Playlists)
- Comments
- Pair Device
- ~~Like/Dislike metadata~~
- ~~Show video description~~
- ~~Video suggestions~~
- ~~Local Playlists~~
- ~~Categories~~
- ~~Captions~~
- ~~Search Suggestions~~
- Local Search History
- Local Watch History
- ~~Fix buttons~~
- ~~Display upload date in search results~~
- ~~Local Subscriptions~~

## Credits
- [ffmpeg](https://ffmpeg.org/) - used for decoding videos
- [WiiMC](https://github.com/dborth/wiimc/) - some code is based on wiimc's code
- [libwiigui](https://github.com/dborth/libgui) - used for the gui
- [mplayer-ce](https://github.com/ExtremsCorner/mplayer-ce) - some code is based on mplayer-ce's code
- [JPEGDEC](https://github.com/bitbank2/JPEGDEC) - used for displaying images and youtube thumbnails
- [curl/libcurl](https://curl.se/) - used for network requests
- [libogc2](https://github.com/extremscorner/libogc2) - used for the c library
- [MbedTLS](https://github.com/Mbed-TLS/mbedtls) - used for ssl/tls
- [wii-curl](https://github.com/AndrewPiroli/wii-curl) - used for using curl on the wii
- [libwiisocket](https://gitlab.com/4TU/libwiisocket) - used for network requests
- [returnyoutubedislike](https://www.returnyoutubedislike.com/) - used for the dislikes
