#ifndef HELPER_SERVICE_H
#define HELPER_SERVICE_H

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"
#include "calendar_types.h"

typedef struct {
    char service_url[APP_URL_LEN];
    time_t generated_at;
    bool using_live_data;
    int day_count;
    day_schedule_t schedules[APP_MAX_DAYS];
    overview_day_t overview[APP_MAX_DAYS];
} helper_snapshot_t;

esp_err_t helper_service_init(const char *service_url);
esp_err_t helper_service_refresh(helper_snapshot_t *out_snapshot);
const day_schedule_t *helper_service_get_day(const helper_snapshot_t *snapshot, int day_index);
const calendar_item_t *helper_service_get_item(const helper_snapshot_t *snapshot, int day_index, int item_index);

#endif
