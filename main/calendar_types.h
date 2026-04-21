#ifndef CALENDAR_TYPES_H
#define CALENDAR_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"

typedef enum {
    CALENDAR_ITEM_EVENT = 0,
    CALENDAR_ITEM_TASK = 1,
} calendar_item_type_t;

typedef struct {
    char id[APP_ID_LEN];
    calendar_item_type_t type;
    bool all_day;
    bool completed;
    char title[APP_TITLE_LEN];
    char start_label[APP_TIME_LEN];
    char end_label[APP_TIME_LEN];
    char location[APP_LOCATION_LEN];
    char source[APP_SOURCE_LEN];
    char detail[APP_DETAIL_LEN];
} calendar_item_t;

typedef struct {
    char day_label[APP_LABEL_LEN];
    int item_count;
    calendar_item_t items[APP_MAX_SCHEDULE_ITEMS];
} day_schedule_t;

typedef struct {
    char weekday[8];
    uint8_t day_of_month;
    char month_name[8];   /* abbreviated month, e.g. "Apr" */
    uint16_t year;        /* 4-digit year, e.g. 2026 */
    bool in_current_month;
    bool has_items;
    uint8_t item_count;
} overview_day_t;

#endif
