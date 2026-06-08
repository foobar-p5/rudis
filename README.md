# rudis yayy
(description taken from the website)  
a pretty simple and minimal (if we don't count the miniaudio lib) music player for the cli, written in C  
i've personally been using it for a day since it was at a point where it could actually be used by someone (date of posting since that time: exactly 1 day || 2026-06-09)

## usage:
```sh
rudis [command]
```
### available commands:
```sh
cue <index> # cue the playlist with matching index. use 'list' to show available playlists
list        # lists available playlists from your config.h
next        # skip to next track
pause       # pause current track
play        # resume paused track
previous    # return to previous track
status      # show current playback status
toggle      # toggle play/pause
```
rudis can also be controlled using signals.  
right now they're hardcoded, but i will most likely change that in the next release.
## available signals:
```
USR1  <-- previous track
USR2  <-- next track
RTMIN <-- toggle play/pause
INT   <-- quit the daemon
```
