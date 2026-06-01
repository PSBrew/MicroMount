#ifndef MM_LOG_H
#define MM_LOG_H

#include "mm_platform.h"

void mm_log_init(bool debug_enabled);
void mm_log_shutdown(void);
void mm_log_set_debug_enabled(bool enabled);

void mm_log_debug(const char *subsystem, const char *fmt, ...);
void mm_log_info(const char *subsystem, const char *fmt, ...);
void mm_log_warn(const char *subsystem, const char *fmt, ...);
void mm_log_error(const char *subsystem, const char *fmt, ...);

void mm_notify_summary(const char *fmt, ...);
void mm_notify_error(const char *fmt, ...);
void mm_notify_debug(const char *fmt, ...);

#endif
