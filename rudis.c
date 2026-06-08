#include <dirent.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <taglib/tag_c.h>
#include <time.h>
#include <unistd.h>

#include "miniaudio.h"

#include "config.h"

#define LENGTH(x) (sizeof(x) / sizeof(x[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

enum { NONE, PLAYING, PAUSED };

typedef struct {
  const char *name;
  const char *path;
  char **files;
  int count;
} Playlist;

static Playlist pls[LENGTH(playlists)];
static ma_engine engine;
static ma_sound sound;
static int hs = 0;
static int state = NONE;
static int running = 1;
static int pli = -1;
static int tri = -1;
static int sigpipe[2];

static int strcmp_sort(const void *a, const void *b) {
  return strcmp(*(const char **)a, *(const char **)b);
}

#if notifications
static void notify(const char *msg) {
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "notify-send rudis \"%s\" -c 1984 -r 1984 -t %d",
           msg, notifications_timeout);
  system(cmd);
}
#else
#define notify(msg) ((void)0)
#endif

static void scan(Playlist *pl) {
  DIR *dir = opendir(pl->path);
  if (!dir)
    return;
  struct dirent *entry;
  char **found = NULL;
  int count = 0;
  while ((entry = readdir(dir)) != NULL) {
    if (entry->d_type == DT_DIR) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;
      char sub[4096];
      snprintf(sub, sizeof(sub), "%s/%s", pl->path, entry->d_name);
      Playlist tmp = {.path = sub};
      scan(&tmp);
      for (int i = 0; i < tmp.count; i++) {
        found = realloc(found, (count + 1) * sizeof(char *));
        found[count++] = tmp.files[i];
      }
      free(tmp.files);
      continue;
    }
    char *ext = strrchr(entry->d_name, '.');
    if (!ext)
      continue;
    ext++;
    int ok = 0;
    for (int i = 0; i < LENGTH(extensions); i++) {
      if (strcasecmp(ext, extensions[i]) == 0) {
        ok = 1;
        break;
      }
    }
    if (!ok)
      continue;
    char full[4096];
    snprintf(full, sizeof(full), "%s/%s", pl->path, entry->d_name);
    found = realloc(found, (count + 1) * sizeof(char *));
    found[count++] = strdup(full);
  }
  closedir(dir);
  qsort(found, count, sizeof(char *), strcmp_sort);
  pl->files = found;
  pl->count = count;
}

static void save_scan(void) {
  FILE *f = fopen(SCAN_FILE, "w");
  if (!f)
    return;
  for (int i = 0; i < LENGTH(playlists); i++) {
    if (!pls[i].files)
      continue;
    for (int j = 0; j < pls[i].count; j++) {
      fprintf(f, "%s\n", pls[i].files[j]);
    }
  }
  fclose(f);
}

static void shuf(Playlist *pl) {
  if (!shuffle || pl->count < 2)
    return;
  for (int i = pl->count - 1; i > 0; i--) {
    int j = rand() % (i + 1);
    char *tmp = pl->files[i];
    pl->files[i] = pl->files[j];
    pl->files[j] = tmp;
  }
}

static char *get_title(const char *path) {
  TagLib_File *file = taglib_file_new(path);
  if (!file || !taglib_file_is_valid(file)) {
    if (file)
      taglib_file_free(file);
    const char *base = strrchr(path, '/');
    if (!base)
      return strdup(path);
    base++;
    char *t = strdup(base);
    char *dot = strrchr(t, '.');
    if (dot)
      *dot = '\0';
    return t;
  }

  TagLib_Tag *tag = taglib_file_tag(file);
  char *title = strdup(taglib_tag_title(tag));
  taglib_tag_free_strings();
  taglib_file_free(file);

  if (!title || strlen(title) == 0) {
    free(title);
    const char *base = strrchr(path, '/');
    if (!base)
      return strdup(path);
    base++;
    char *t = strdup(base);
    char *dot = strrchr(t, '.');
    if (dot)
      *dot = '\0';
    return t;
  }

  return title;
}

static void play_file(const char *path) {
  if (hs) {
    ma_sound_uninit(&sound);
    hs = 0;
  }
  ma_result r = ma_sound_init_from_file(&engine, path, 0, NULL, NULL, &sound);
  if (r == MA_SUCCESS) {
    ma_sound_start(&sound);
    hs = 1;
    state = PLAYING;
  }
}

static void toggle(void) {
  if (state == NONE)
    return;
  if (state == PLAYING) {
    ma_sound_stop(&sound);
    state = PAUSED;
  } else if (state == PAUSED) {
    ma_sound_start(&sound);
    state = PLAYING;
  }
}

static void next(void) {
  if (pli < 0 || tri < 0)
    return;
  if (hs) {
    ma_sound_uninit(&sound);
    hs = 0;
  }
  tri++;
  if (tri >= pls[pli].count) {
    if (loop) {
      tri = 0;
    } else {
      state = NONE;
      return;
    }
  }
  play_file(pls[pli].files[tri]);
}

static void prev(void) {
  if (pli < 0 || tri < 0)
    return;
  if (hs) {
    ma_sound_uninit(&sound);
    hs = 0;
  }
  tri--;
  if (tri < 0) {
    if (loop) {
      tri = pls[pli].count - 1;
    } else {
      tri = 0;
    }
  }
  play_file(pls[pli].files[tri]);
}

static void handle_sig(int sig) {
  int s = sig;
  write(sigpipe[1], &s, sizeof(int));
}

static void setup_signals(void) {
  pipe(sigpipe);
  fcntl(sigpipe[0], F_SETFL, O_NONBLOCK);
  fcntl(sigpipe[1], F_SETFL, O_NONBLOCK);

  struct sigaction sa;
  sa.sa_handler = handle_sig;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;

  sigaction(SIGUSR1, &sa, NULL);
  sigaction(SIGUSR2, &sa, NULL);
  sigaction(SIGRTMIN, &sa, NULL);
  sigaction(SIGINT, &sa, NULL);
}

static void process_signals(void) {
  int sig;
  while (read(sigpipe[0], &sig, sizeof(int)) > 0) {
    if (sig == SIGUSR1) {
      prev();
      if (pli >= 0 && tri >= 0) {
        char *t = get_title(pls[pli].files[tri]);
        char buf[512];
        snprintf(buf, sizeof(buf), "now playing: %s", t);
        free(t);
        notify(buf);
      }
    } else if (sig == SIGUSR2) {
      next();
      if (pli >= 0 && tri >= 0) {
        char *t = get_title(pls[pli].files[tri]);
        char buf[512];
        snprintf(buf, sizeof(buf), "now playing: %s", t);
        free(t);
        notify(buf);
      }
    } else if (sig == 34) {
      toggle();
      notify(state == PLAYING ? "resumed" : "paused");
    } else if (sig == SIGINT) {
      running = 0;
    }
  }
}

static void handle_client(int fd) {
  char cmd[256];
  int n = read(fd, cmd, sizeof(cmd) - 1);
  if (n <= 0) {
    close(fd);
    return;
  }
  cmd[n] = '\0';
  char resp[4096] = "";

  if (strcmp(cmd, "list") == 0) {
    char *p = resp;
    for (int i = 0; i < LENGTH(playlists); i++) {
      p += sprintf(p, "[%d] %s\n", i, playlists[i].name);
    }
  } else if (strncmp(cmd, "cue ", 4) == 0) {
    int idx = atoi(cmd + 4);
    if (idx >= 0 && idx < LENGTH(playlists)) {
      if (!pls[idx].files) {
        pls[idx].name = playlists[idx].name;
        pls[idx].path = playlists[idx].path;
        scan(&pls[idx]);
        shuf(&pls[idx]);
        save_scan();
      }
      pli = idx;
      tri = 0;
      if (hs) {
        ma_sound_uninit(&sound);
        hs = 0;
      }
      state = NONE;
      if (pls[pli].count > 0) {
        play_file(pls[pli].files[tri]);
        char *t = get_title(pls[pli].files[tri]);
        snprintf(resp, sizeof(resp), "cued [%s]\n", playlists[idx].name);
        char buf[512];
        snprintf(buf, sizeof(buf), "now playing: %s", t);
        free(t);
        notify(buf);
      }
    }
  } else if (strcmp(cmd, "play") == 0) {
    if (pli >= 0 && pls[pli].count > 0) {
      if (state == PAUSED) {
        ma_sound_start(&sound);
        state = PLAYING;
        notify("resumed");
      } else if (state != PLAYING) {
        play_file(pls[pli].files[tri]);
        char *t = get_title(pls[pli].files[tri]);
        char buf[512];
        snprintf(buf, sizeof(buf), "now playing: %s", t);
        free(t);
        notify(buf);
      }
    }
  } else if (strcmp(cmd, "pause") == 0) {
    if (state == PLAYING) {
      ma_sound_stop(&sound);
      state = PAUSED;
      notify("paused");
    }
  } else if (strcmp(cmd, "toggle") == 0) {
    toggle();
    notify(state == PLAYING ? "resumed" : "paused");
  } else if (strcmp(cmd, "next") == 0) {
    next();
    if (pli >= 0 && tri >= 0) {
      char *t = get_title(pls[pli].files[tri]);
      snprintf(resp, sizeof(resp), "now playing: %s\n", t);
      notify(resp);
      free(t);
    }
  } else if (strcmp(cmd, "previous") == 0) {
    prev();
    if (pli >= 0 && tri >= 0) {
      char *t = get_title(pls[pli].files[tri]);
      snprintf(resp, sizeof(resp), "now playing: %s\n", t);
      notify(resp);
      free(t);
    }
  } else if (strcmp(cmd, "status") == 0) {
    float pos = 0, dur = 0;
    if (hs) {
      ma_sound_get_cursor_in_seconds(&sound, &pos);
      ma_sound_get_length_in_seconds(&sound, &dur);
    }
    char *status_str = state == NONE      ? "idle"
                       : state == PLAYING ? "playing"
                                          : "paused";
    const char *plname = pli >= 0 ? pls[pli].name : "none";
    char track_buf[256] = "none";
    if (hs && pli >= 0 && tri >= 0) {
      char *t = get_title(pls[pli].files[tri]);
      snprintf(track_buf, sizeof(track_buf), "%s", t);
      free(t);
    }
    snprintf(resp, sizeof(resp), "%s: [%s] %s\n%d:%02d/%d:%02d\n", status_str,
             plname, track_buf, (int)pos / 60, (int)pos % 60, (int)dur / 60,
             (int)dur % 60);
  }

  write(fd, resp, strlen(resp));
  close(fd);
}

static int client_mode(int argc, char **argv) {
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  strcpy(addr.sun_path, SOCKET_PATH);
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    fprintf(stderr, "rudis daemon is not running\n");
    return 1;
  }

  char cmd[256] = "";
  if (strcmp(argv[1], "list") == 0) {
    strcpy(cmd, "list");
  } else if (strcmp(argv[1], "cue") == 0 && argc > 2) {
    snprintf(cmd, sizeof(cmd), "cue %s", argv[2]);
  } else if (strcmp(argv[1], "play") == 0) {
    strcpy(cmd, "play");
  } else if (strcmp(argv[1], "pause") == 0) {
    strcpy(cmd, "pause");
  } else if (strcmp(argv[1], "toggle") == 0) {
    strcpy(cmd, "toggle");
  } else if (strcmp(argv[1], "next") == 0) {
    strcpy(cmd, "next");
  } else if (strcmp(argv[1], "previous") == 0) {
    strcpy(cmd, "previous");
  } else if (strcmp(argv[1], "status") == 0) {
    strcpy(cmd, "status");
  } else {
    fprintf(stderr, "unknown command\n");
    return 1;
  }

  write(fd, cmd, strlen(cmd));
  char resp[4096];
  int n = read(fd, resp, sizeof(resp) - 1);
  if (n > 0) {
    resp[n] = '\0';
    printf("%s", resp);
  }
  close(fd);
  return 0;
}

int main(int argc, char **argv) {
  if (argc > 1 &&
      (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
    system("man rudis");
    return 0;
  }

  if (argc > 1)
    return client_mode(argc, argv);

  setup_signals();
  ma_engine_init(NULL, &engine);

  int server = socket(AF_UNIX, SOCK_STREAM, 0);
  struct sockaddr_un addr = {.sun_family = AF_UNIX};
  strcpy(addr.sun_path, SOCKET_PATH);
  unlink(SOCKET_PATH);
  bind(server, (struct sockaddr *)&addr, sizeof(addr));
  listen(server, 5);
  srand(time(NULL));

  sigset_t mask, oldmask;
  sigemptyset(&mask);
  sigaddset(&mask, SIGINT);
  sigaddset(&mask, SIGUSR1);
  sigaddset(&mask, SIGUSR2);
  sigaddset(&mask, SIGRTMIN);
  sigprocmask(SIG_BLOCK, &mask, &oldmask);

  while (running) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(server, &fds);
    FD_SET(sigpipe[0], &fds);
    int maxfd = MAX(server, sigpipe[0]);

    struct timespec ts = {.tv_sec = 0, .tv_nsec = 100000000};
    int ret = pselect(maxfd + 1, &fds, NULL, NULL, &ts, &oldmask);

    if (ret < 0)
      continue;

    if (FD_ISSET(sigpipe[0], &fds))
      process_signals();
    if (FD_ISSET(server, &fds)) {
      int client = accept(server, NULL, NULL);
      handle_client(client);
    }

    if (hs && ma_sound_at_end(&sound)) {
      ma_sound_uninit(&sound);
      hs = 0;
      next();
      if (pli >= 0 && tri >= 0) {
        char *t = get_title(pls[pli].files[tri]);
        char buf[512];
        snprintf(buf, sizeof(buf), "now playing: %s", t);
        free(t);
        notify(buf);
      }
    }
  }

  close(server);
  unlink(SOCKET_PATH);
  ma_engine_uninit(&engine);

  for (int i = 0; i < LENGTH(playlists); i++) {
    if (pls[i].files) {
      for (int j = 0; j < pls[i].count; j++)
        free(pls[i].files[j]);
      free(pls[i].files);
    }
  }

  return 0;
}
