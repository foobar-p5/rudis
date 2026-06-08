static const struct {
    const char *name;
    const char *path;
} playlists[] = {
    /* example of valid playlists
    { "dg-01", "/mnt/media/music/death_grips" },
    { "slvt", "/home/ethicalgooner2007/Music/" },
    */
};

/* extensions to search for in a playlist */
static const char *extensions[] = { "mp3", "flac", "ogg", "wav" };

/*----------VARIABLES----------*/
static const int shuffle = 1;
static const int loop = 1;

#define notifications 0 /* you need libnotify for notifs to work */
#define notifications_timeout 1000 /* 1000ms=1s, so 2000 is 2 seconds */
/*--------VARIABLES-END--------*/

#define SOCKET_PATH "/tmp/rudis.sock"
#define SCAN_FILE "/tmp/rudis_scanned"
