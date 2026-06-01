#ifndef MM_PATHS_H
#define MM_PATHS_H

#define MM_ROOT_DIR "/data/micromount"
#define MM_CONFIG_FILE MM_ROOT_DIR "/config.ini"
#define MM_LOG_FILE MM_ROOT_DIR "/debug.log"
#define MM_MANAGED_PREFIX "micromount-"

#define MM_DEFAULT_SCAN_PATHS_INITIALIZER                                     \
  {                                                                           \
    "/data/homebrew", "/data/etaHEN/games",                                   \
        "/mnt/ext0/homebrew", "/mnt/ext0/etaHEN/games",                       \
        "/mnt/ext1/homebrew", "/mnt/ext1/etaHEN/games",                       \
        "/mnt/usb0/homebrew", "/mnt/usb1/homebrew", "/mnt/usb2/homebrew",     \
        "/mnt/usb3/homebrew", "/mnt/usb4/homebrew", "/mnt/usb5/homebrew",     \
        "/mnt/usb6/homebrew", "/mnt/usb7/homebrew",                           \
        "/mnt/usb0/etaHEN/games", "/mnt/usb1/etaHEN/games",                   \
        "/mnt/usb2/etaHEN/games", "/mnt/usb3/etaHEN/games",                   \
        "/mnt/usb4/etaHEN/games", "/mnt/usb5/etaHEN/games",                   \
        "/mnt/usb6/etaHEN/games", "/mnt/usb7/etaHEN/games",                   \
        "/mnt/usb0", "/mnt/usb1", "/mnt/usb2", "/mnt/usb3", "/mnt/usb4",      \
        "/mnt/usb5", "/mnt/usb6", "/mnt/usb7", "/mnt/ext0", "/mnt/ext1",      \
        NULL                                                                   \
  }

#endif
