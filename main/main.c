#include <stdio.h>
#include <string.h>
#include <time.h>

#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "app_config.h"
#include "button_input.h"
#include "display_layer.h"
#include "helper_service.h"
#include "wifi_manager.h"

static const char *TAG = "t5_calendar";

typedef enum {
    UI_MODE_OVERVIEW = 0,
    UI_MODE_DETAIL = 1,
} ui_mode_t;

typedef struct {
    helper_snapshot_t snapshot;
    int selected_day_index;
    int selected_item_index;
    ui_mode_t mode;
    int render_count;
} app_state_t;

EXT_RAM_BSS_ATTR static app_state_t s_state;

static void format_timestamp(time_t when, char *buffer, size_t length)
{
    if (!buffer || length == 0) {
        return;
    }

    if (when <= 0) {
        snprintf(buffer, length, "waiting/offline");
        return;
    }

    snprintf(buffer, length, "uptime %llus", (unsigned long long)when);
}

static void ensure_valid_selection(app_state_t *state)
{
    if (state->selected_day_index < 0) {
        state->selected_day_index = 0;
    }
    if (state->selected_day_index >= state->snapshot.day_count) {
        state->selected_day_index = state->snapshot.day_count - 1;
    }

    const day_schedule_t *schedule = helper_service_get_day(&state->snapshot, state->selected_day_index);
    if (!schedule || schedule->item_count == 0) {
        state->selected_item_index = -1;
        state->mode = UI_MODE_OVERVIEW;
        return;
    }

    if (state->selected_item_index < 0) {
        state->selected_item_index = 0;
    }
    if (state->selected_item_index >= schedule->item_count) {
        state->selected_item_index = schedule->item_count - 1;
    }
}

static void log_button_strip(const app_state_t *state)
{
    ESP_LOGI(TAG, "Buttons | [1] Prev  [2] Next  [3] Select/Back  [4] Up  [5] Down");
    ESP_LOGI(TAG, "Button IO | prev=%s next=%s select=%s up=%s down=%s",
             button_input_is_available(BUTTON_ACTION_PREV) ? "ready" : "reserved",
             button_input_is_available(BUTTON_ACTION_NEXT) ? "ready" : "reserved",
             button_input_is_available(BUTTON_ACTION_SELECT) ? "ready" : "reserved",
             button_input_is_available(BUTTON_ACTION_UP) ? "ready" : "reserved",
             button_input_is_available(BUTTON_ACTION_DOWN) ? "ready" : "reserved");
    ESP_LOGI(TAG, "Mode     | %s", state->mode == UI_MODE_DETAIL ? "detail" : "overview");
}

static void log_left_pane(const app_state_t *state)
{
    const day_schedule_t *schedule = helper_service_get_day(&state->snapshot, state->selected_day_index);
    if (!schedule) {
        ESP_LOGW(TAG, "Left pane unavailable: invalid selected day");
        return;
    }

    ESP_LOGI(TAG, "Left 1/3 | Agenda for %s", schedule->day_label);
    if (schedule->item_count == 0) {
        ESP_LOGI(TAG, "  No events or tasks for this day.");
        return;
    }

    for (int i = 0; i < schedule->item_count; ++i) {
        const calendar_item_t *item = &schedule->items[i];
        const char *selected = (i == state->selected_item_index) ? ">" : " ";
        const char *kind = (item->type == CALENDAR_ITEM_TASK) ? "TASK" : "EVT ";
        ESP_LOGI(TAG, "  %s [%s] %s-%s  %s",
                 selected,
                 kind,
                 item->start_label,
                 item->end_label[0] ? item->end_label : "--",
                 item->title);
    }
}

static void log_right_pane(const app_state_t *state)
{
    if (state->mode == UI_MODE_DETAIL) {
        const calendar_item_t *item = helper_service_get_item(&state->snapshot,
                                                              state->selected_day_index,
                                                              state->selected_item_index);
        if (!item) {
            ESP_LOGI(TAG, "Right 2/3 | No item selected yet");
            return;
        }

        ESP_LOGI(TAG, "Right 2/3 | Detail view");
        ESP_LOGI(TAG, "  Title    : %s", item->title);
        ESP_LOGI(TAG, "  Time     : %s - %s", item->start_label,
                 item->end_label[0] ? item->end_label : "--");
        ESP_LOGI(TAG, "  Source   : %s", item->source);
        ESP_LOGI(TAG, "  Location : %s", item->location[0] ? item->location : "n/a");
        ESP_LOGI(TAG, "  Notes    : %s", item->detail);
        return;
    }

    ESP_LOGI(TAG, "Right 2/3 | Four-week overview");
    for (int week = 0; week < 4; ++week) {
        char row[192] = {0};
        size_t used = 0;

        for (int day = 0; day < 7; ++day) {
            int index = (week * 7) + day;
            const overview_day_t *cell = &state->snapshot.overview[index];
            used += (size_t)snprintf(row + used, sizeof(row) - used,
                                     "%s %02u(%u)%s  ",
                                     cell->weekday,
                                     cell->day_of_month,
                                     cell->item_count,
                                     index == state->selected_day_index ? "*" : " ");
            if (used >= sizeof(row)) {
                break;
            }
        }

        ESP_LOGI(TAG, "  %s", row);
    }
}

static void render_dashboard(const app_state_t *state)
{
    char generated_at[32] = {0};
    format_timestamp(state->snapshot.generated_at, generated_at, sizeof(generated_at));

    display_render_request_t request = {
        .snapshot = &state->snapshot,
        .selected_day_index = state->selected_day_index,
        .selected_item_index = state->selected_item_index,
        .detail_mode = (state->mode == UI_MODE_DETAIL),
        .wifi_status = wifi_manager_status_text(),
    };

    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "T5 Family Calendar | landscape %dx%d", APP_SCREEN_WIDTH, APP_SCREEN_HEIGHT);
    ESP_LOGI(TAG, "Helper service     | %s", state->snapshot.service_url);
    ESP_LOGI(TAG, "Wi-Fi status        | %s", wifi_manager_status_text());
    ESP_LOGI(TAG, "Data source         | %s", state->snapshot.using_live_data ? "live helper service" : "waiting/offline");
    ESP_LOGI(TAG, "Last refresh        | %s", generated_at);
    log_button_strip(state);
    log_left_pane(state);
    log_right_pane(state);
    display_layer_render(&request);
    ESP_LOGI(TAG, "Framebuffer checksum | %lu", (unsigned long)display_layer_framebuffer_checksum());
}

static void refresh_snapshot(app_state_t *state)
{
    ESP_ERROR_CHECK(helper_service_refresh(&state->snapshot));
    ensure_valid_selection(state);
    render_dashboard(state);
}

static void apply_button_action(app_state_t *state, button_action_t action)
{
    const day_schedule_t *schedule = helper_service_get_day(&state->snapshot, state->selected_day_index);

    switch (action) {
        case BUTTON_ACTION_PREV:
            if (state->mode == UI_MODE_DETAIL && schedule && schedule->item_count > 1) {
                state->selected_item_index--;
            } else {
                state->selected_day_index--;
            }
            break;
        case BUTTON_ACTION_NEXT:
            if (state->mode == UI_MODE_DETAIL && schedule && schedule->item_count > 1) {
                state->selected_item_index++;
            } else {
                state->selected_day_index++;
            }
            break;
        case BUTTON_ACTION_UP:
            state->selected_day_index -= 7;
            break;
        case BUTTON_ACTION_DOWN:
            state->selected_day_index += 7;
            break;
        case BUTTON_ACTION_SELECT:
            if (state->mode == UI_MODE_OVERVIEW && schedule && schedule->item_count > 0) {
                state->mode = UI_MODE_DETAIL;
                state->selected_item_index = 0;
            } else {
                state->mode = UI_MODE_OVERVIEW;
            }
            break;
        case BUTTON_ACTION_NONE:
        default:
            break;
    }

    ensure_valid_selection(state);
    render_dashboard(state);
}

void app_main(void)
{
    app_state_t *state = &s_state;
    memset(state, 0, sizeof(*state));
    int64_t last_refresh_us = 0;

    ESP_LOGI(TAG, "Starting T5 Family Calendar foundation for the ESP32 WROVER-E board");
    ESP_LOGI(TAG, "Snapshot storage reserved in static memory: %u bytes", (unsigned)sizeof(helper_snapshot_t));

    esp_err_t wdt_err = esp_task_wdt_deinit();
    if (wdt_err == ESP_OK) {
        ESP_LOGW(TAG, "Task watchdog disabled during hardware display bring-up");
    }

    esp_err_t display_err = display_layer_init();
    if (display_err != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(display_err));
    }
    ESP_ERROR_CHECK(button_input_init());
    ESP_ERROR_CHECK(helper_service_init(NULL));

    state->selected_day_index = 0;
    state->selected_item_index = 0;
    state->mode = UI_MODE_OVERVIEW;
    state->render_count = 0;

    refresh_snapshot(state);
    last_refresh_us = esp_timer_get_time();

#if APP_ENABLE_WIFI
    esp_err_t wifi_err = wifi_manager_init();
    if (wifi_err == ESP_OK && wifi_manager_is_configured()) {
        if (wifi_manager_wait_for_connection(10000) == ESP_OK) {
            wifi_manager_sync_time(5000);
            refresh_snapshot(state);
            last_refresh_us = esp_timer_get_time();
        } else {
            ESP_LOGW(TAG, "Wi-Fi connection timed out; continuing with cached/mock snapshot");
        }
    } else if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi init failed: %s; continuing offline", esp_err_to_name(wifi_err));
    }
#else
    ESP_LOGW(TAG, "Wi-Fi temporarily disabled during hardware display bring-up");
#endif

    while (1) {
        button_action_t action = button_input_poll();
        if (action != BUTTON_ACTION_NONE) {
            ESP_LOGI(TAG, "Button action: %s", button_input_name(action));
            apply_button_action(state, action);
        }

        int64_t now_us = esp_timer_get_time();
        if ((now_us - last_refresh_us) > (60LL * 1000000LL)) {
            refresh_snapshot(state);
            last_refresh_us = now_us;
        }

        vTaskDelay(pdMS_TO_TICKS(120));
    }
}
