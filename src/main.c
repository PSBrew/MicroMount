#include "mm_config.h"
#include "mm_log.h"
#include "mm_mount.h"
#include "mm_paths.h"
#include "mm_platform.h"
#include "mm_scan.h"

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

static bool mm_should_stop(void) {
  return g_stop_requested != 0;
}

static bool mm_sleep_with_stop(unsigned int seconds) {
  unsigned int remaining_us = seconds * 1000000u;

  while (remaining_us > 0) {
    unsigned int step = remaining_us > 200000u ? 200000u : remaining_us;
    if (mm_should_stop())
      return true;
    (void)sceKernelUsleep(step);
    remaining_us -= step;
  }

  return mm_should_stop();
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
  if (config_reloaded)
    mm_notify_debug("Reloaded %s", MM_CONFIG_FILE);

  if (!mm_scan_for_images(config, &candidates))
    summary.errors++;
  summary.discovered = candidates.count;

  mm_cleanup_managed_mounts(config, &candidates, &summary.cleaned,
                            &summary.errors);
  mm_reconcile_mounts(config, &candidates, &summary.mounted, &summary.skipped,
                      &summary.errors);
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

  mm_candidate_list_free(&candidates);
}

int main(void) {
  mm_config_t config;
  bool user_service_initialized = false;
  bool first_cycle = true;
  size_t last_notified_total = SIZE_MAX;

  kernel_set_ucred_authid(-1, MM_AUTHID_BASE);
  (void)syscall(SYS_thr_set_name, -1, MM_PAYLOAD_NAME);
  mm_install_signal_handlers();

  mm_log_init(true);
  if (sceUserServiceInitialize(NULL) == 0)
    user_service_initialized = true;
  else
    mm_log_warn("CORE", "sceUserServiceInitialize failed; notifications may be limited");

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
