#include "helper_service.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "app_config.h"
#include "esp_attr.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "wifi_manager.h"
#include "cJSON.h"

static const char *TAG = "helper_service";
static char g_service_url[APP_URL_LEN] = HELPER_SERVICE_URL_DEFAULT;
#define HELPER_JSON_BUFFER_CAPACITY (64 * 1024)
EXT_RAM_BSS_ATTR static char g_json_buffer[HELPER_JSON_BUFFER_CAPACITY];
EXT_RAM_BSS_ATTR static helper_snapshot_t g_working_snapshot;

typedef struct {
    char *data;
    size_t len;
    size_t capacity;
    bool truncated;
} http_buffer_t;

static void fill_item(calendar_item_t *item,
                      const char *id,
                      calendar_item_type_t type,
                      bool all_day,
                      bool completed,
                      const char *title,
                      const char *start_label,
                      const char *end_label,
                      const char *location,
                      const char *source,
                      const char *detail)
{
    if (!item) {
        return;
    }

    memset(item, 0, sizeof(*item));
    snprintf(item->id, sizeof(item->id), "%s", id ? id : "");
    item->type = type;
    item->all_day = all_day;
    item->completed = completed;
    snprintf(item->title, sizeof(item->title), "%s", title ? title : "");
    snprintf(item->start_label, sizeof(item->start_label), "%s", start_label ? start_label : "");
    snprintf(item->end_label, sizeof(item->end_label), "%s", end_label ? end_label : "");
    snprintf(item->location, sizeof(item->location), "%s", location ? location : "");
    snprintf(item->source, sizeof(item->source), "%s", source ? source : "");
    snprintf(item->detail, sizeof(item->detail), "%s", detail ? detail : "");
}

static const char *weekday_for_index(int day_index)
{
    static const char *k_weekdays[7] = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};
    return k_weekdays[day_index % 7];
}

static uint8_t day_number_for_index(int day_index)
{
    return (uint8_t)(((17 + day_index - 1) % 30) + 1);
}

static void fill_overview_day(overview_day_t *day, int day_index, int item_count)
{
    memset(day, 0, sizeof(*day));
    snprintf(day->weekday, sizeof(day->weekday), "%s", weekday_for_index(day_index));
    day->day_of_month = day_number_for_index(day_index);
    day->in_current_month = true;
    day->has_items = item_count > 0;
    day->item_count = (uint8_t)item_count;
}

static void build_day_schedule(day_schedule_t *schedule, int day_index)
{
    memset(schedule, 0, sizeof(*schedule));
    snprintf(schedule->day_label, sizeof(schedule->day_label), "%s %02u Apr",
             weekday_for_index(day_index), day_number_for_index(day_index));

    if (day_index == 0) {
        schedule->item_count = 4;
        fill_item(&schedule->items[0], "evt-001", CALENDAR_ITEM_EVENT, false, false,
                  "School run", "08:15", "08:45", "Local school", "Family",
                  "Morning school drop-off for the children.");
        fill_item(&schedule->items[1], "evt-002", CALENDAR_ITEM_EVENT, false, false,
                  "Project review", "10:00", "11:00", "Home office", "Work",
                  "Weekly review call with action items and follow-ups.");
        fill_item(&schedule->items[2], "tsk-001", CALENDAR_ITEM_TASK, true, false,
                  "Order groceries", "Any", "", "", "Shared Tasks",
                  "Place the weekly grocery order before 5pm.");
        fill_item(&schedule->items[3], "evt-003", CALENDAR_ITEM_EVENT, false, false,
                  "Football practice", "17:30", "18:30", "Sports centre", "Family",
                  "Take boots and water bottle.");
        return;
    }

    if (day_index == 1) {
        schedule->item_count = 3;
        fill_item(&schedule->items[0], "evt-004", CALENDAR_ITEM_EVENT, false, false,
                  "Dentist", "09:00", "09:30", "High Street Dental", "Personal",
                  "Routine checkup appointment.");
        fill_item(&schedule->items[1], "tsk-002", CALENDAR_ITEM_TASK, true, false,
                  "Pay utility bill", "Any", "", "", "Admin",
                  "Electricity bill due tomorrow.");
        fill_item(&schedule->items[2], "evt-005", CALENDAR_ITEM_EVENT, false, false,
                  "Family dinner", "18:00", "20:00", "Grandma's house", "Family",
                  "Birthday meal and cake.");
        return;
    }

    if ((day_index % 7) == 2) {
        schedule->item_count = 2;
        fill_item(&schedule->items[0], "evt-weekly", CALENDAR_ITEM_EVENT, false, false,
                  "Bin night", "19:00", "19:15", "Home", "Household",
                  "Put recycling and general waste out.");
        fill_item(&schedule->items[1], "tsk-weekly", CALENDAR_ITEM_TASK, true, false,
                  "Check school letters", "Any", "", "", "Family",
                  "Review notices and sign forms if needed.");
        return;
    }

    if ((day_index % 5) == 0) {
        schedule->item_count = 1;
        fill_item(&schedule->items[0], "tsk-plan", CALENDAR_ITEM_TASK, true, false,
                  "Meal planning", "Any", "", "", "Household",
                  "Plan dinners for the next few days.");
        return;
    }

    if ((day_index % 3) == 0) {
        schedule->item_count = 1;
        fill_item(&schedule->items[0], "evt-club", CALENDAR_ITEM_EVENT, false, false,
                  "Club activity", "16:00", "17:00", "Community hall", "Family",
                  "Standing after-school activity.");
    }
}

static void build_mock_snapshot(helper_snapshot_t *snapshot)
{
    memset(snapshot, 0, sizeof(*snapshot));
    snprintf(snapshot->service_url, sizeof(snapshot->service_url), "%s", g_service_url);
    snapshot->generated_at = 0;
    snapshot->day_count = APP_MAX_DAYS;
    snapshot->using_live_data = false;

    time_t now = time(NULL);
    bool has_valid_time = now > 1700000000;

    for (int i = 0; i < APP_MAX_DAYS; ++i) {
        day_schedule_t *schedule = &snapshot->schedules[i];
        overview_day_t *overview = &snapshot->overview[i];
        memset(schedule, 0, sizeof(*schedule));
        memset(overview, 0, sizeof(*overview));

        if (has_valid_time) {
            time_t current = now + (time_t)(i * 86400);
            struct tm tm_info = {0};
            localtime_r(&current, &tm_info);
            strftime(schedule->day_label, sizeof(schedule->day_label), "%a %d %b", &tm_info);
            strftime(overview->weekday, sizeof(overview->weekday), "%a", &tm_info);
            strftime(overview->month_name, sizeof(overview->month_name), "%b", &tm_info);
            overview->day_of_month = (uint8_t)tm_info.tm_mday;
            overview->year = (uint16_t)(tm_info.tm_year + 1900);
            overview->in_current_month = true;
        } else {
            snprintf(schedule->day_label, sizeof(schedule->day_label), "%s", i == 0 ? "WAITING FOR DATA" : "");
            snprintf(overview->weekday, sizeof(overview->weekday), "%s", "--");
            overview->day_of_month = 0;
            overview->in_current_month = true;
        }

        schedule->item_count = 0;
        overview->has_items = false;
        overview->item_count = 0;
    }
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buffer_t *buffer = (http_buffer_t *)evt->user_data;

    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0 && buffer) {
        size_t remaining = (buffer->capacity > buffer->len) ? (buffer->capacity - buffer->len - 1) : 0;
        size_t copy_len = evt->data_len < remaining ? (size_t)evt->data_len : remaining;
        if ((size_t)evt->data_len > remaining) {
            buffer->truncated = true;
        }
        if (copy_len > 0) {
            memcpy(buffer->data + buffer->len, evt->data, copy_len);
            buffer->len += copy_len;
            buffer->data[buffer->len] = '\0';
        }
    }

    return ESP_OK;
}

static esp_err_t fetch_snapshot_json(char *json_buffer, size_t json_capacity)
{
    if (!json_buffer || json_capacity < 8) {
        return ESP_ERR_INVALID_ARG;
    }

    char url[APP_URL_LEN + 32] = {0};
    snprintf(url, sizeof(url), "%s%s", g_service_url, APP_HELPER_SNAPSHOT_ENDPOINT);

    http_buffer_t buffer = {
        .data = json_buffer,
        .len = 0,
        .capacity = json_capacity,
        .truncated = false,
    };
    json_buffer[0] = '\0';

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .event_handler = http_event_handler,
        .user_data = &buffer,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "Helper service request failed: err=%s status=%d", esp_err_to_name(err), status_code);
        return err == ESP_OK ? ESP_FAIL : err;
    }

    if (buffer.truncated) {
        ESP_LOGW(TAG, "Helper response exceeded buffer (%u bytes), truncated at %u bytes",
                 (unsigned)json_capacity, (unsigned)buffer.len);
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Downloaded %u bytes from helper service", (unsigned)buffer.len);
    return ESP_OK;
}

static const char *json_string_or(cJSON *object, const char *name, const char *fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && item->valuestring ? item->valuestring : fallback;
}

static bool json_bool_or(cJSON *object, const char *name, bool fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item);
    }
    return fallback;
}

static int json_int_or(cJSON *object, const char *name, int fallback)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? item->valueint : fallback;
}

static void parse_item_json(cJSON *item_json, calendar_item_t *item)
{
    const char *type_text = json_string_or(item_json, "type", "event");
    calendar_item_type_t type = (strcmp(type_text, "task") == 0) ? CALENDAR_ITEM_TASK : CALENDAR_ITEM_EVENT;

    fill_item(item,
              json_string_or(item_json, "id", ""),
              type,
              json_bool_or(item_json, "all_day", false),
              json_bool_or(item_json, "completed", false),
              json_string_or(item_json, "title", "Untitled"),
              json_string_or(item_json, "start", ""),
              json_string_or(item_json, "end", ""),
              json_string_or(item_json, "location", ""),
              json_string_or(item_json, "source", ""),
              json_string_or(item_json, "detail", ""));
}

static void parse_day_json(cJSON *day_json, helper_snapshot_t *snapshot)
{
    int offset = json_int_or(day_json, "offset", -1);
    if (offset < 0 || offset >= APP_MAX_DAYS) {
        return;
    }

    day_schedule_t *schedule = &snapshot->schedules[offset];
    overview_day_t *overview = &snapshot->overview[offset];

    const char *label = json_string_or(day_json, "label", schedule->day_label);
    snprintf(schedule->day_label, sizeof(schedule->day_label), "%s", label);
    snprintf(overview->weekday, sizeof(overview->weekday), "%s", json_string_or(day_json, "weekday", overview->weekday));
    overview->day_of_month = (uint8_t)json_int_or(day_json, "day", overview->day_of_month);
    snprintf(overview->month_name, sizeof(overview->month_name), "%s", json_string_or(day_json, "month", overview->month_name));
    overview->year = (uint16_t)json_int_or(day_json, "year", overview->year);

    cJSON *items = cJSON_GetObjectItemCaseSensitive(day_json, "items");
    if (cJSON_IsArray(items)) {
        int count = cJSON_GetArraySize(items);
        schedule->item_count = (count > APP_MAX_SCHEDULE_ITEMS) ? APP_MAX_SCHEDULE_ITEMS : count;
        for (int i = 0; i < schedule->item_count; ++i) {
            cJSON *item_json = cJSON_GetArrayItem(items, i);
            if (cJSON_IsObject(item_json)) {
                parse_item_json(item_json, &schedule->items[i]);
            }
        }
    }

    overview->has_items = schedule->item_count > 0;
    overview->item_count = (uint8_t)schedule->item_count;
}

static esp_err_t overlay_remote_snapshot(const char *json_text, helper_snapshot_t *snapshot)
{
    cJSON *root = cJSON_Parse(json_text);
    if (!root) {
        return ESP_FAIL;
    }

    cJSON *days = cJSON_GetObjectItemCaseSensitive(root, "days");
    if (!cJSON_IsArray(days)) {
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    int count = cJSON_GetArraySize(days);
    snapshot->day_count = (count > APP_MAX_DAYS) ? APP_MAX_DAYS : count;
    snapshot->generated_at = (time_t)(esp_timer_get_time() / 1000000LL);
    snapshot->using_live_data = true;

    for (int i = 0; i < snapshot->day_count; ++i) {
        cJSON *day_json = cJSON_GetArrayItem(days, i);
        if (cJSON_IsObject(day_json)) {
            parse_day_json(day_json, snapshot);
        }
    }

    cJSON_Delete(root);
    return ESP_OK;
}

esp_err_t helper_service_init(const char *service_url)
{
    if (service_url && service_url[0] != '\0') {
        snprintf(g_service_url, sizeof(g_service_url), "%s", service_url);
    }
    return ESP_OK;
}

esp_err_t helper_service_refresh(helper_snapshot_t *out_snapshot)
{
    if (!out_snapshot) {
        return ESP_ERR_INVALID_ARG;
    }

    bool had_existing_data = out_snapshot->day_count > 0;
    if (!had_existing_data) {
        build_mock_snapshot(out_snapshot);
    }

#if APP_ENABLE_WIFI
    if (wifi_manager_is_connected()) {
        helper_snapshot_t *working_snapshot = out_snapshot;
        if (had_existing_data) {
            memcpy(&g_working_snapshot, out_snapshot, sizeof(g_working_snapshot));
            working_snapshot = &g_working_snapshot;
        }

        memset(g_json_buffer, 0, sizeof(g_json_buffer));
        esp_err_t err = fetch_snapshot_json(g_json_buffer, sizeof(g_json_buffer));
        if (err == ESP_OK) {
            if (overlay_remote_snapshot(g_json_buffer, working_snapshot) == ESP_OK) {
                if (working_snapshot != out_snapshot) {
                    memcpy(out_snapshot, working_snapshot, sizeof(*out_snapshot));
                }
                ESP_LOGI(TAG, "Loaded snapshot from helper service");
                return ESP_OK;
            }
            ESP_LOGW(TAG, "Remote JSON parse failed, keeping previous snapshot");
        } else {
            ESP_LOGW(TAG, "Keeping previous snapshot after helper fetch failure");
        }
    } else {
        ESP_LOGI(TAG, "Wi-Fi not connected yet, skipping helper fetch");
    }
#endif

    if (had_existing_data) {
        ESP_LOGI(TAG, "Using last known good snapshot");
    } else {
        ESP_LOGI(TAG, "Using placeholder snapshot until live data is available");
    }
    return ESP_OK;
}

const day_schedule_t *helper_service_get_day(const helper_snapshot_t *snapshot, int day_index)
{
    if (!snapshot || day_index < 0 || day_index >= snapshot->day_count) {
        return NULL;
    }
    return &snapshot->schedules[day_index];
}

const calendar_item_t *helper_service_get_item(const helper_snapshot_t *snapshot, int day_index, int item_index)
{
    const day_schedule_t *schedule = helper_service_get_day(snapshot, day_index);
    if (!schedule || item_index < 0 || item_index >= schedule->item_count) {
        return NULL;
    }
    return &schedule->items[item_index];
}
