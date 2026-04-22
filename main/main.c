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
#include "battery_monitor.h"
#include "board_config.h"
#include "button_input.h"
#include "display_layer.h"
#include "helper_service.h"
#include "wifi_manager.h"
#include "esp_sleep.h"
#include "driver/gpio.h"

static const char *TAG = "t5_calendar";

/* Navigation state persisted in ESP32 RTC slow SRAM across deep sleep */
typedef struct {
    uint32_t magic;
    int32_t  selected_day_index;
    int32_t  selected_item_index;
    uint32_t mode;
    int32_t  render_count;
} rtc_persistent_t;

#define APP_RTC_MAGIC 0xCA1E4747UL

RTC_DATA_ATTR static rtc_persistent_t s_rtc;

typedef enum {
    UI_MODE_OVERVIEW = 0,
    UI_MODE_DETAIL = 1,
} ui_mode_t;

typedef struct {
    helper_snapshot_t snapshot;
    helper_snapshot_t last_rendered_snapshot;
    int selected_day_index;
    int selected_item_index;
    int last_rendered_day_index;
    int last_rendered_item_index;
    ui_mode_t mode;
    ui_mode_t last_rendered_mode;
    char last_wifi_status[128];
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

static bool snapshot_visual_equals(const helper_snapshot_t *a, const helper_snapshot_t *b)
{
    if (!a || !b) {
        return false;
    }

    return a->using_live_data == b->using_live_data &&
           a->day_count == b->day_count &&
           memcmp(a->schedules, b->schedules, sizeof(a->schedules)) == 0 &&
           memcmp(a->overview, b->overview, sizeof(a->overview)) == 0;
}

static void log_button_strip(const app_state_t *state)
{
    ESP_LOGI(TAG, "Buttons | [IO35] Prev  [IO34] Next  [IO39] Select  (no Home pin - IO0 is EPD strapping pin)");
    ESP_LOGI(TAG, "Button IO | prev=%s next=%s select=%s",
             button_input_is_available(BUTTON_ACTION_PREV)   ? "ready" : "unavailable",
             button_input_is_available(BUTTON_ACTION_NEXT)   ? "ready" : "unavailable",
             button_input_is_available(BUTTON_ACTION_SELECT) ? "ready" : "unavailable");
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

static void render_dashboard(app_state_t *state)
{
    const char *wifi_status_raw = wifi_manager_status_text();
    char batt_str[16];
    battery_monitor_format(batt_str, sizeof(batt_str));
    char wifi_status_buf[128];
    snprintf(wifi_status_buf, sizeof(wifi_status_buf), "%.60s  |  %s",
             wifi_status_raw ? wifi_status_raw : "", batt_str);
    const char *wifi_status = wifi_status_buf;
    bool needs_refresh = (state->render_count == 0) ||
                         !snapshot_visual_equals(&state->snapshot, &state->last_rendered_snapshot) ||
                         state->selected_day_index != state->last_rendered_day_index ||
                         state->selected_item_index != state->last_rendered_item_index ||
                         state->mode != state->last_rendered_mode ||
                         strncmp(state->last_wifi_status, wifi_status, sizeof(state->last_wifi_status)) != 0;

    if (!needs_refresh) {
        ESP_LOGI(TAG, "No visible dashboard changes; skipping E-paper refresh");
        return;
    }

    char generated_at[32] = {0};
    format_timestamp(state->snapshot.generated_at, generated_at, sizeof(generated_at));

    bool nav_changed = state->selected_day_index  != state->last_rendered_day_index  ||
                       state->selected_item_index != state->last_rendered_item_index ||
                       state->mode                != state->last_rendered_mode;
    bool data_changed = !snapshot_visual_equals(&state->snapshot, &state->last_rendered_snapshot) ||
                        strncmp(state->last_wifi_status, wifi_status, sizeof(state->last_wifi_status)) != 0;

    /* Navigation moves highlights and clears panels — e-paper requires a full clear to remove
     * existing dark pixels.  Background data updates (same layout, new content) can use a
     * fast redraw to avoid unnecessary flicker. */
    char datetime_buf[24];
    {
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        strftime(datetime_buf, sizeof(datetime_buf), "%a %d %b %Y %H:%M", tm_info);
    }

    display_render_request_t request = {
        .snapshot = &state->snapshot,
        .selected_day_index = state->selected_day_index,
        .selected_item_index = state->selected_item_index,
        .detail_mode = (state->mode == UI_MODE_DETAIL),
        .wifi_status = wifi_status,
        .datetime_str = datetime_buf,
        .full_refresh = (state->render_count == 0) || nav_changed || data_changed,
    };

    ESP_LOGI(TAG, "============================================================");
    ESP_LOGI(TAG, "T5 Family Calendar | landscape %dx%d", APP_SCREEN_WIDTH, APP_SCREEN_HEIGHT);
    ESP_LOGI(TAG, "Helper service     | %s", state->snapshot.service_url);
    ESP_LOGI(TAG, "Wi-Fi status        | %s", wifi_status);
    ESP_LOGI(TAG, "Data source         | %s", state->snapshot.using_live_data ? "live helper service" : "waiting/offline");
    ESP_LOGI(TAG, "Last refresh        | %s", generated_at);
    log_button_strip(state);
    log_left_pane(state);
    log_right_pane(state);
    display_layer_render(&request);
    ESP_LOGI(TAG, "Framebuffer checksum | %lu", (unsigned long)display_layer_framebuffer_checksum());

    memcpy(&state->last_rendered_snapshot, &state->snapshot, sizeof(state->snapshot));
    state->last_rendered_day_index = state->selected_day_index;
    state->last_rendered_item_index = state->selected_item_index;
    state->last_rendered_mode = state->mode;
    snprintf(state->last_wifi_status, sizeof(state->last_wifi_status), "%s", wifi_status ? wifi_status : "unknown");
    state->render_count++;
}

static void refresh_snapshot(app_state_t *state)
{
    ESP_ERROR_CHECK(helper_service_refresh(&state->snapshot));
    ensure_valid_selection(state);
    render_dashboard(state);
}

/* Update navigation state for a button action without triggering a render */
static void state_apply_button(app_state_t *state, button_action_t action)
{
    const day_schedule_t *schedule = helper_service_get_day(&state->snapshot, state->selected_day_index);
    switch (action) {
        case BUTTON_ACTION_PREV:
            if (state->mode == UI_MODE_DETAIL && schedule && schedule->item_count > 1)
                state->selected_item_index--;
            else
                state->selected_day_index--;
            break;
        case BUTTON_ACTION_NEXT:
            if (state->mode == UI_MODE_DETAIL && schedule && schedule->item_count > 1)
                state->selected_item_index++;
            else
                state->selected_day_index++;
            break;
        case BUTTON_ACTION_SELECT:
            if (state->mode == UI_MODE_OVERVIEW && schedule && schedule->item_count > 0) {
                state->mode = UI_MODE_DETAIL;
                state->selected_item_index = 0;
            } else {
                state->mode = UI_MODE_OVERVIEW;
            }
            break;
        case BUTTON_ACTION_HOME:
            state->selected_day_index = 0;
            state->selected_item_index = 0;
            state->mode = UI_MODE_OVERVIEW;
            break;
        default:
            break;
    }
    ensure_valid_selection(state);
}

static void apply_button_action(app_state_t *state, button_action_t action)
{
    state_apply_button(state, action);
    render_dashboard(state);
}

void app_main(void)
{
    esp_sleep_wakeup_cause_t wakeup = esp_sleep_get_wakeup_cause();
    const bool first_boot  = (wakeup == ESP_SLEEP_WAKEUP_UNDEFINED);
    const bool timer_wake  = (wakeup == ESP_SLEEP_WAKEUP_TIMER);
    const bool button_wake = (wakeup == ESP_SLEEP_WAKEUP_EXT0 ||
                              wakeup == ESP_SLEEP_WAKEUP_EXT1);
    (void)timer_wake;   /* used only in log message */

    app_state_t *state = &s_state;
    memset(state, 0, sizeof(*state));

    ESP_LOGI(TAG, "== T5 Calendar == wake: %s",
             first_boot ? "first-boot" : timer_wake ? "timer" : "button");

    /* Restore navigation state from RTC SRAM (survives deep sleep) */
    if (!first_boot && s_rtc.magic == APP_RTC_MAGIC) {
        state->selected_day_index  = (int)s_rtc.selected_day_index;
        state->selected_item_index = (int)s_rtc.selected_item_index;
        state->mode                = (ui_mode_t)s_rtc.mode;
        state->render_count        = (int)s_rtc.render_count;
        ESP_LOGI(TAG, "RTC state: day=%d item=%d mode=%d renders=%d",
                 state->selected_day_index, state->selected_item_index,
                 (int)state->mode, state->render_count);
    }

    esp_task_wdt_deinit();

    esp_err_t display_err = display_layer_init(/*skip_splash=*/!first_boot);
    if (display_err != ESP_OK) {
        ESP_LOGE(TAG, "Display init failed: %s", esp_err_to_name(display_err));
    }
    esp_err_t batt_err = battery_monitor_init();
    if (batt_err != ESP_OK) {
        ESP_LOGW(TAG, "Battery monitor init failed: %s", esp_err_to_name(batt_err));
    }
    ESP_ERROR_CHECK(button_input_init());
    ESP_ERROR_CHECK(helper_service_init(NULL));

    /* Seed with placeholder data so snapshot is always valid even if WiFi fails */
    ESP_ERROR_CHECK(helper_service_refresh(&state->snapshot));
    ensure_valid_selection(state);

#if APP_ENABLE_WIFI
    esp_err_t wifi_err = wifi_manager_init();
    if (wifi_err == ESP_OK && wifi_manager_is_configured()) {
        int timeout_ms = first_boot ? 15000 : 10000;
        if (wifi_manager_wait_for_connection(timeout_ms) == ESP_OK) {
            wifi_manager_sync_time(5000);
            int retries = first_boot ? 4 : 1;
            for (int r = 0; r < retries; r++) {
                helper_service_refresh(&state->snapshot);
                ensure_valid_selection(state);
                if (state->snapshot.using_live_data) break;
                if (r < retries - 1) {
                    ESP_LOGI(TAG, "Live data not ready, retrying in 8s (%d/%d)...", r + 1, retries);
                    vTaskDelay(pdMS_TO_TICKS(8000));
                }
            }
        } else {
            ESP_LOGW(TAG, "Wi-Fi timed out; rendering with cached snapshot");
        }
    } else if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "Wi-Fi init failed: %s", esp_err_to_name(wifi_err));
    }
#else
    ESP_LOGW(TAG, "Wi-Fi disabled");
#endif

    /* Apply the button action that caused this wake, before the first render */
    button_action_t wake_action = BUTTON_ACTION_NONE;
    if (wakeup == ESP_SLEEP_WAKEUP_EXT0) {
        wake_action = BUTTON_ACTION_SELECT;              /* GPIO39 = SELECT */
    } else if (wakeup == ESP_SLEEP_WAKEUP_EXT1) {
        wake_action = BUTTON_ACTION_NEXT;   /* only NEXT (GPIO34) is on EXT1 */
    }
    if (wake_action != BUTTON_ACTION_NONE) {
        ESP_LOGI(TAG, "Wake button action: %s", button_input_name(wake_action));
        state_apply_button(state, wake_action);
    }

    render_dashboard(state);

    /* Interactive window: stay awake and respond to buttons while user is active */
    if (button_wake) {
        int64_t last_activity = esp_timer_get_time();
        ESP_LOGI(TAG, "Interactive window (%ds inactivity timeout)", APP_INTERACTIVE_TIMEOUT_S);
        while ((esp_timer_get_time() - last_activity) <
               ((int64_t)APP_INTERACTIVE_TIMEOUT_S * 1000000LL)) {
            button_action_t act = button_input_poll();
            if (act != BUTTON_ACTION_NONE) {
                ESP_LOGI(TAG, "Button: %s", button_input_name(act));
                apply_button_action(state, act);
                last_activity = esp_timer_get_time();
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        ESP_LOGI(TAG, "Interactive timeout, entering deep sleep");
    }

    /* Persist navigation state to RTC SRAM before deep sleep */
    s_rtc.magic              = APP_RTC_MAGIC;
    s_rtc.selected_day_index  = (int32_t)state->selected_day_index;
    s_rtc.selected_item_index = (int32_t)state->selected_item_index;
    s_rtc.mode               = (uint32_t)state->mode;
    s_rtc.render_count       = (int32_t)state->render_count;

#if APP_ENABLE_WIFI
    wifi_manager_stop();
#endif

    /* Configure wakeup sources:
     *   Timer  - refresh data every APP_SLEEP_REFRESH_S seconds
     *   EXT0   - GPIO39 (SELECT) wakes on LOW (button pressed)
     *   EXT1   - GPIO34 (NEXT) wakes on LOW (single-pin ALL_LOW = that pin LOW) */
    esp_sleep_enable_timer_wakeup((uint64_t)APP_SLEEP_REFRESH_S * 1000000ULL);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_39, 0);
    esp_sleep_enable_ext1_wakeup(1ULL << BOARD_BUTTON_NEXT_PIN, ESP_EXT1_WAKEUP_ALL_LOW);

    ESP_LOGI(TAG, "Deep sleep for %ds (SELECT or NEXT wake early)", APP_SLEEP_REFRESH_S);
    vTaskDelay(pdMS_TO_TICKS(50));   /* allow log output to flush */
    esp_deep_sleep_start();
}
