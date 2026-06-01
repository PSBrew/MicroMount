#ifndef MM_SCAN_H
#define MM_SCAN_H

#include "mm_types.h"

void mm_candidate_list_init(mm_candidate_list_t *list);
void mm_candidate_list_free(mm_candidate_list_t *list);
bool mm_scan_for_images(const mm_config_t *config, mm_candidate_list_t *list);

#endif
