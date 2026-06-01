#include "mm_config.h"
#include "mm_log.h"
#include "mm_mount.h"
#include "mm_paths.h"
#include "mm_platform.h"
#include "mm_runtime.h"
#include "mm_scan.h"

#include <sys/sysctl.h>
#include <sys/user.h>

#define MM_RESTART_WAIT_POLL_US 200000u
#define MM_RESTART_WAIT_TIMEOUT_US 15000000u

static volatile sig_atomic_t g_stop_requested = 0;

static void mm_on_signal(int signal_number) {
  (void)signal_number;
  g_stop_requested = 1;
}

static void mm_install_signal_handlers(void) {
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  action.sa_handler = mm_on_signal;
  sigemptyset(&action.sa_mask);
  sigaction(SIGTERM, &action, NULL);
  sigaction(SIGINT, &action, NULL);
  sigaction(SIGHUP, &action, NULL);
  sigaction(SIGQUIT, &action, NULL);
}

bool mm_should_stop(void) {
  return g_stop_requested != 0;
}

static bool mm_sleep_with_stop_us(unsigned int total_us) {
  unsigned int remaining_us = total_us;

  while (remaining_us > 0) {
    unsigned int step = remaining_us > 200000u ? 200000u : remaining_us;
    if (mm_should_stop())
      return true;
    (void)sceKernelUsleep(step);
    remaining_us -= step;
  }

  return mm_should_stop();
}

static bool mm_sleep_with_stop(unsigned int seconds) {
  if (seconds > UINT_MAX / 1000000u)
    return mm_sleep_with_stop_us(UINT_MAX);
  return mm_sleep_with_stop_us(seconds * 1000000u);
}

static pid_t mm_find_pid_by_name(const char *name, bool exclude_self) {
  int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PROC, 0};
  size_t buffer_size = 0;
  uint8_t *buffer = NULL;
  uint8_t *cursor;
  uint8_t *end;
  size_t name_length;
  pid_t self_pid = exclude_self ? getpid() : -1;
  pid_t found_pid = 0;

  if (!name)
    return -1;

  name_length = strlen(name);
  if (name_length >= sizeof(((struct kinfo_proc *)0)->ki_tdname))
    return 0;

  if (sysctl(mib, 4, NULL, &buffer_size, NULL, 0) != 0)
    return -1;
  if (buffer_size == 0)
    return 0;

  buffer = malloc(buffer_size);
  if (!buffer)
    return -1;

  if (sysctl(mib, 4, buffer, &buffer_size, NULL, 0) != 0) {
    free(buffer);
    return -1;
  }

  cursor = buffer;
  end = buffer + buffer_size;
  while (cursor < end) {
    const struct kinfo_proc *entry;
    int entry_size;
    size_t thread_name_length;

    if ((size_t)(end - cursor) < sizeof(int))
      break;

    entry_size = *(const int *)cursor;
    if (entry_size <= 0 || (size_t)entry_size > (size_t)(end - cursor))
      break;

    if ((size_t)entry_size <
        offsetof(struct kinfo_proc, ki_tdname) +
            sizeof(((struct kinfo_proc *)0)->ki_tdname)) {
      cursor += entry_size;
      continue;
    }

    entry = (const struct kinfo_proc *)(const void *)cursor;
    thread_name_length =
        strnlen(entry->ki_tdname, sizeof(entry->ki_tdname));
    if ((!exclude_self || entry->ki_pid != self_pid) &&
        thread_name_length == name_length &&
        memcmp(entry->ki_tdname, name, name_length) == 0) {
      found_pid = entry->ki_pid;
      break;
    }

    cursor += entry_size;
  }

  free(buffer);
  return found_pid;
}

static bool mm_terminate_existing_processes(void) {
  pid_t target_pid;
  pid_t active_pid = 0;
  unsigned int waited_us = 0;

  target_pid = mm_find_pid_by_name(MM_PAYLOAD_NAME, true);
  if (target_pid < 0)
    return false;
  if (target_pid == 0)
    return true;

  mm_notify_debug(
      "Found an existing MicroMount process. Attempting to terminate it.");

  while (!mm_should_stop()) {
    target_pid = mm_find_pid_by_name(MM_PAYLOAD_NAME, true);
    if (target_pid < 0)
      return false;
    if (target_pid == 0)
      return true;

    if (target_pid != active_pid) {
      active_pid = target_pid;
      waited_us = 0;
    }

    if (waited_us >= MM_RESTART_WAIT_TIMEOUT_US) {
      if (kill(target_pid, SIGKILL) != 0 && errno != ESRCH)
        return false;
      if (waited_us >= MM_RESTART_WAIT_TIMEOUT_US + 2000000u)
        return false;
    } else {
      if (kill(target_pid, SIGTERM) != 0 && errno != ESRCH)
        return false;
    }

    if (mm_sleep_with_stop_us(MM_RESTART_WAIT_POLL_US))
      return false;
    waited_us += MM_RESTART_WAIT_POLL_US;
  }

  return false;
}

static void mm_run_cycle(mm_config_t *config, bool first_cycle,
                         size_t *last_notified_total) {
  mm_candidate_list_t candidates;
  mm_cycle_summary_t summary;
  bool config_reloaded = false;

  mm_candidate_list_init(&candidates);
  memset(&summary, 0, sizeof(summary));

  if (!mm_config_reload_if_changed(config, &config_reloaded))
    summary.errors++;
  if (mm_should_stop())
    goto cleanup;
  if (config_reloaded)
    mm_notify_debug("Reloaded %s", MM_CONFIG_FILE);

  if (!mm_scan_for_images(config, &candidates) && !mm_should_stop())
    summary.errors++;
  if (mm_should_stop())
    goto cleanup;
  summary.discovered = candidates.count;

  mm_cleanup_managed_mounts(config, &candidates, &summary.cleaned,
                            &summary.errors);
  if (mm_should_stop())
    goto cleanup;
  mm_reconcile_mounts(config, &candidates, &summary.mounted, &summary.skipped,
                      &summary.errors);
  if (mm_should_stop())
    goto cleanup;
  summary.total_mounted = mm_count_managed_mounts(config);

  mm_log_info("CYCLE",
              "summary discovered=%zu mounted=%zu skipped=%zu cleaned=%zu "
              "errors=%zu total_mounted=%zu",
              summary.discovered, summary.mounted, summary.skipped,
              summary.cleaned, summary.errors, summary.total_mounted);

  if (first_cycle || summary.mounted > 0 || summary.cleaned > 0 ||
      summary.errors > 0 || summary.total_mounted != *last_notified_total) {
    mm_notify_summary(
        "MicroMount synchronized.\nMounted: %zu\nNew: %zu  Skipped: %zu  "
        "Cleaned: %zu  Errors: %zu",
        summary.total_mounted, summary.mounted, summary.skipped,
        summary.cleaned, summary.errors);
    *last_notified_total = summary.total_mounted;
  }

cleanup:
  mm_candidate_list_free(&candidates);
}

int main(void) {
  mm_config_t config;
  bool user_service_initialized = false;
  bool replaced_previous_process = false;
  bool first_cycle = true;
  size_t last_notified_total = SIZE_MAX;
  pid_t existing_pid;

  kernel_set_ucred_authid(-1, MM_AUTHID_BASE);
  mm_install_signal_handlers();
  if (sceUserServiceInitialize(NULL) == 0)
    user_service_initialized = true;
  else
    mm_log_warn("CORE", "sceUserServiceInitialize failed; notifications may be limited");

  existing_pid = mm_find_pid_by_name(MM_PAYLOAD_NAME, true);
  if (existing_pid < 0) {
    mm_log_error("CORE", "failed to enumerate running processes");
    if (user_service_initialized)
      sceUserServiceTerminate();
    return 1;
  }
  if (existing_pid > 0) {
    if (!mm_terminate_existing_processes()) {
      mm_log_error("CORE", "failed to stop previous MicroMount process");
      if (user_service_initialized)
        sceUserServiceTerminate();
      return 1;
    }
    replaced_previous_process = true;
  }

  (void)syscall(SYS_thr_set_name, -1, MM_PAYLOAD_NAME);
  mm_log_init(true);
  if (replaced_previous_process) {
    mm_notify_debug(
        "Previous process terminated. Starting a new micromount process...");
  }

  if (!mm_config_load(&config)) {
    mm_log_error("CORE", "failed to load config, continuing with defaults");
    mm_config_init_defaults(&config);
  }

  mm_log_info("CORE", "MicroMount %s starting", MICROMOUNT_VERSION);
  mm_notify_debug("MicroMount %s started", MICROMOUNT_VERSION);

  while (!mm_should_stop()) {
    mm_run_cycle(&config, first_cycle, &last_notified_total);
    first_cycle = false;
    if (mm_should_stop())
      break;
    if (mm_sleep_with_stop(config.scan_interval_seconds))
      break;
  }

  mm_log_info("CORE", "MicroMount stopping");
  mm_notify_debug("MicroMount stopped");
  mm_log_shutdown();
  if (user_service_initialized)
    sceUserServiceTerminate();
  return 0;
}
