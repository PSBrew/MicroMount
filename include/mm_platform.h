#ifndef MM_PLATFORM_H
#define MM_PLATFORM_H

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>

#include <ps5/kernel.h>

#ifndef MICROMOUNT_VERSION
#define MICROMOUNT_VERSION "unknown"
#endif

#define MM_PAYLOAD_NAME "micromount.elf"
#define MM_AUTHID_BASE 0x4800000000000006ull
#define MM_NOTIFICATION_SYSTEM_USER_ID 0xFE

typedef struct notify_request {
  char unused[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);
int sceKernelUsleep(unsigned int microseconds);
int sceUserServiceInitialize(void *);
void sceUserServiceTerminate(void);
int sceNotificationSend(int userId, bool isLoggedIn, const char *payload);

#endif
