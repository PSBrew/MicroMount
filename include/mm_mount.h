#ifndef MM_MOUNT_H
#define MM_MOUNT_H

#include "mm_types.h"

void mm_cleanup_managed_mounts(const mm_config_t *config,
                               const mm_candidate_list_t *candidates,
                               size_t *cleaned_out, size_t *errors_out);
void mm_reconcile_mounts(const mm_config_t *config,
                         const mm_candidate_list_t *candidates,
                         size_t *mounted_out, size_t *skipped_out,
                         size_t *errors_out);
size_t mm_count_managed_mounts(const mm_config_t *config);

#endif
