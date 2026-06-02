#include "mm_log.h"

#include "mm_paths.h"
#include "mm_util.h"

#include <stdarg.h>

static FILE *g_log_file = NULL;
static bool g_debug_enabled = true;
static bool g_log_initialized = false;
static bool g_notification_icon_checked = false;
static bool g_notification_icon_ready = false;

#define MM_LOG_FILE_1 MM_LOG_FILE ".1"
#define MM_LOG_FILE_2 MM_LOG_FILE ".2"
#define MM_LOG_FILE_3 MM_LOG_FILE ".3"

extern unsigned char icon_png[] __attribute__((weak));
extern unsigned int icon_png_len __attribute__((weak));
extern unsigned char assets_icon_png[] __attribute__((weak));
extern unsigned int assets_icon_png_len __attribute__((weak));

static bool mm_get_notification_icon(const unsigned char **icon_data,
                                     size_t *icon_size) {
  if (!icon_data || !icon_size)
    return false;

  if (icon_png && icon_png_len > 0u) {
    *icon_data = icon_png;
    *icon_size = (size_t)icon_png_len;
    return true;
  }

  if (assets_icon_png && assets_icon_png_len > 0u) {
    *icon_data = assets_icon_png;
    *icon_size = (size_t)assets_icon_png_len;
    return true;
  }

  return false;
}

static FILE *mm_ensure_log_file_open(void) {
  if (g_log_file)
    return g_log_file;

  (void)mm_ensure_dir_recursive(MM_ROOT_DIR);
  g_log_file = fopen(MM_LOG_FILE, "a");
  return g_log_file;
}

static void mm_rotate_file_if_present(const char *from, const char *to) {
  if (rename(from, to) != 0 && errno != ENOENT) {
    fprintf(stdout, "[WARN] [LOG] failed to rotate %s to %s: %s\n", from, to,
            strerror(errno));
    fflush(stdout);
  }
}

static void mm_rotate_log_files(void) {
  if (unlink(MM_LOG_FILE_3) != 0 && errno != ENOENT) {
    fprintf(stdout, "[WARN] [LOG] failed to remove %s: %s\n", MM_LOG_FILE_3,
            strerror(errno));
    fflush(stdout);
  }

  mm_rotate_file_if_present(MM_LOG_FILE_2, MM_LOG_FILE_3);
  mm_rotate_file_if_present(MM_LOG_FILE_1, MM_LOG_FILE_2);
  mm_rotate_file_if_present(MM_LOG_FILE, MM_LOG_FILE_1);
}

static void mm_build_timestamp(char out[32]) {
  time_t now;
  struct tm local_tm;

  now = time(NULL);
  if (localtime_r(&now, &local_tm) == NULL) {
    (void)mm_copy_string(out, 32, "1970-01-01 00:00:00");
    return;
  }

  (void)strftime(out, 32, "%Y-%m-%d %H:%M:%S", &local_tm);
}

static void mm_append_json_escaped(char *dst, size_t dst_size, const char *src) {
  size_t used = strlen(dst);

  if (!dst || !src || used >= dst_size)
    return;

  while (*src != '\0' && used + 1 < dst_size) {
    const char *escape = NULL;
    char single[2] = {0, 0};
    size_t escape_len;

    switch (*src) {
    case '\\':
      escape = "\\\\";
      break;
    case '"':
      escape = "\\\"";
      break;
    case '\n':
      escape = "\\n";
      break;
    case '\r':
      escape = "\\r";
      break;
    case '\t':
      escape = "\\t";
      break;
    default:
      single[0] = *src;
      escape = single;
      break;
    }

    escape_len = strlen(escape);
    if (used + escape_len >= dst_size)
      break;

    memcpy(dst + used, escape, escape_len);
    used += escape_len;
    dst[used] = '\0';
    ++src;
  }
}

static bool mm_build_notification_timestamp(char out[32]) {
  time_t now;
  struct tm utc_tm;

  now = time(NULL);
  if (gmtime_r(&now, &utc_tm) == NULL)
    return false;

  return strftime(out, 32, "%Y-%m-%dT%H:%M:%S.000Z", &utc_tm) != 0;
}

static void mm_log_emit_v(const char *level, const char *subsystem,
                          const char *fmt, va_list args) {
  FILE *fp;
  char timestamp[32];
  va_list file_args;

  mm_build_timestamp(timestamp);

  va_copy(file_args, args);
  fprintf(stdout, "[%s] [%s] [%s] ", timestamp, level, subsystem);
  vfprintf(stdout, fmt, args);
  fputc('\n', stdout);
  fflush(stdout);

  fp = NULL;
  if (g_log_initialized)
    fp = mm_ensure_log_file_open();
  if (fp) {
    fprintf(fp, "[%s] [%s] [%s] ", timestamp, level, subsystem);
    vfprintf(fp, fmt, file_args);
    fputc('\n', fp);
    fflush(fp);
  }

  va_end(file_args);
}

static void mm_log_emit(const char *level, const char *subsystem,
                        const char *fmt, ...) {
  va_list args;

  va_start(args, fmt);
  mm_log_emit_v(level, subsystem, fmt, args);
  va_end(args);
}

static void mm_notify_plain_message(const char *message) {
  notify_request_t request;

  memset(&request, 0, sizeof(request));
  (void)mm_copy_string(request.message, sizeof(request.message), message);
  (void)sceKernelSendNotificationRequest(0, &request, sizeof(request), 0);
}

static bool mm_ensure_notification_icon_present(void) {
  struct stat st;
  FILE *fp;
  const unsigned char *icon_data;
  size_t icon_size;
  size_t written;
  int saved_errno = 0;

  if (stat(MM_NOTIFY_ICON_FILE, &st) == 0 && st.st_size > 0)
    return true;

  (void)mkdir("/user/data", 0777);
  (void)mkdir(MM_NOTIFY_ICON_DIR, 0777);

  fp = fopen(MM_NOTIFY_ICON_FILE, "wb");
  if (!fp)
    return false;

  if (!mm_get_notification_icon(&icon_data, &icon_size)) {
    (void)fclose(fp);
    errno = ENOENT;
    return false;
  }

  written = fwrite(icon_data, 1, icon_size, fp);
  if (written != icon_size)
    saved_errno = ferror(fp) ? errno : EIO;
  if (fflush(fp) != 0 && saved_errno == 0)
    saved_errno = errno;
  if (fclose(fp) != 0 && saved_errno == 0)
    saved_errno = errno;

  if (saved_errno != 0) {
    errno = saved_errno;
    (void)unlink(MM_NOTIFY_ICON_FILE);
    return false;
  }

  return true;
}

static bool mm_notifications_init(void) {
  if (!g_notification_icon_checked) {
    g_notification_icon_ready = mm_ensure_notification_icon_present();
    g_notification_icon_checked = true;
    if (!g_notification_icon_ready) {
      mm_log_emit("WARN", "NOTIFY", "failed to prepare icon at %s: %s",
                  MM_NOTIFY_ICON_FILE, strerror(errno));
    }
  }

  return g_notification_icon_ready;
}

static bool mm_notify_rich_message(const char *message) {
  char payload[8192];
  char created_at[32];
  char notification_id[32];
  char escaped_message[4096];
  char escaped_version[128];
  int written;

  if (!message || message[0] == '\0')
    return false;
  if (!mm_notifications_init())
    return false;
  if (!mm_build_notification_timestamp(created_at))
    return false;

  escaped_message[0] = '\0';
  escaped_version[0] = '\0';
  mm_append_json_escaped(escaped_message, sizeof(escaped_message), message);
  mm_append_json_escaped(escaped_version, sizeof(escaped_version),
                         MICROMOUNT_VERSION);
  snprintf(notification_id, sizeof(notification_id), "%u",
           (unsigned)((uint32_t)time(NULL) ^ (uint32_t)getpid()));

  written = snprintf(
      payload, sizeof(payload),
      "{"
      "\"rawData\":{"
      "\"viewTemplateType\":\"InteractiveToastTemplateB\","
      "\"channelType\":\"ServiceFeedback\","
      "\"bundleName\":\"MicroMount\","
      "\"useCaseId\":\"IDC\","
      "\"soundEffect\":\"none\","
      "\"toastOverwriteType\":\"InQueue\","
      "\"isImmediate\":true,"
      "\"priority\":100,"
      "\"viewData\":{"
      "\"icon\":{\"type\":\"Url\",\"parameters\":{\"url\":\"" MM_NOTIFY_ICON_FILE
      "\"}},"
      "\"message\":{\"body\":\"%s\"},"
      "\"subMessage\":{\"body\":\"MicroMount %s\"}"
      "}"
      "},"
      "\"createdDateTime\":\"%s\","
      "\"localNotificationId\":\"%s\""
      "}",
      escaped_message, escaped_version, created_at, notification_id);
  if (written < 0 || (size_t)written >= sizeof(payload))
    return false;

  return sceNotificationSend(MM_NOTIFICATION_SYSTEM_USER_ID, true, payload) == 0;
}

static void mm_notify_v(bool allow_when_debug_disabled, const char *fmt,
                        va_list args) {
  char message[3075];

  if (!allow_when_debug_disabled && !g_debug_enabled)
    return;

  vsnprintf(message, sizeof(message), fmt, args);
  if (!mm_notify_rich_message(message))
    mm_notify_plain_message(message);
  mm_log_emit("INFO", "NOTIFY", "%s", message);
}

void mm_log_init(bool debug_enabled) {
  g_debug_enabled = debug_enabled;
  if (g_log_file) {
    fclose(g_log_file);
    g_log_file = NULL;
  }
  g_log_initialized = true;
  (void)mm_ensure_dir_recursive(MM_ROOT_DIR);
  mm_rotate_log_files();
  (void)mm_ensure_log_file_open();
}

void mm_log_shutdown(void) {
  if (g_log_file) {
    fclose(g_log_file);
    g_log_file = NULL;
  }
  g_log_initialized = false;
}

void mm_log_set_debug_enabled(bool enabled) {
  g_debug_enabled = enabled;
}

void mm_log_debug(const char *subsystem, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  mm_log_emit_v("DEBUG", subsystem, fmt, args);
  va_end(args);
}

void mm_log_info(const char *subsystem, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  mm_log_emit_v("INFO", subsystem, fmt, args);
  va_end(args);
}

void mm_log_warn(const char *subsystem, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  mm_log_emit_v("WARN", subsystem, fmt, args);
  va_end(args);
}

void mm_log_error(const char *subsystem, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  mm_log_emit_v("ERROR", subsystem, fmt, args);
  va_end(args);
}

void mm_notify_summary(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  mm_notify_v(true, fmt, args);
  va_end(args);
}

void mm_notify_error(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  mm_notify_v(true, fmt, args);
  va_end(args);
}

void mm_notify_debug(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  mm_notify_v(false, fmt, args);
  va_end(args);
}
