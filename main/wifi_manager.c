#include "wifi_manager.h"

#include <string.h>
#include <time.h>

#include "app_config.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_manager";
static EventGroupHandle_t s_wifi_event_group;
static bool s_wifi_connected = false;
static bool s_wifi_started = false;
static esp_netif_t *s_sta_netif = NULL;

#define WIFI_CONNECTED_BIT BIT0

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        if (wifi_manager_is_configured()) {
            esp_wifi_connect();
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        if (s_wifi_event_group) {
            xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        ESP_LOGW(TAG, "Wi-Fi disconnected");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        s_wifi_connected = true;
        if (s_wifi_event_group) {
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        }
        ESP_LOGI(TAG, "Wi-Fi connected and IP acquired");
    }
}

bool wifi_manager_is_configured(void)
{
    return (strlen(APP_WIFI_SSID) > 0);
}

bool wifi_manager_is_connected(void)
{
    return s_wifi_connected;
}

const char *wifi_manager_status_text(void)
{
#if !APP_ENABLE_WIFI
    return "offline";
#else
    if (!wifi_manager_is_configured()) {
        return "not configured";
    }
    return s_wifi_connected ? "connected" : "connecting";
#endif
}

esp_err_t wifi_manager_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    if (!s_sta_netif) {
        s_sta_netif = esp_netif_create_default_wifi_sta();
    }

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.static_rx_buf_num = 6;
    cfg.dynamic_rx_buf_num = 8;
    cfg.dynamic_tx_buf_num = 8;

    ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));

    wifi_config_t wifi_config = {0};
    snprintf((char *)wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid), "%s", APP_WIFI_SSID);
    snprintf((char *)wifi_config.sta.password, sizeof(wifi_config.sta.password), "%s", APP_WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_started = true;

    if (!wifi_manager_is_configured()) {
        ESP_LOGW(TAG, "APP_WIFI_SSID is empty. Edit app_config.h to enable network features.");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting Wi-Fi connection to configured SSID");
    return ESP_OK;
}

esp_err_t wifi_manager_wait_for_connection(int timeout_ms)
{
    if (!wifi_manager_is_configured()) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_wifi_connected) {
        return ESP_OK;
    }

    if (!s_wifi_started) {
        return ESP_ERR_INVALID_STATE;
    }

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT,
                                           pdFALSE,
                                           pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t wifi_manager_stop(void)
{
    if (!s_wifi_started) {
        return ESP_OK;
    }
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_wifi_stop();
    s_wifi_started = false;
    s_wifi_connected = false;
    ESP_LOGI(TAG, "Wi-Fi stopped");
    return ESP_OK;
}

esp_err_t wifi_manager_sync_time(int timeout_ms)
{
    if (!s_wifi_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    setenv("TZ", APP_TIME_ZONE, 1);
    tzset();

    /* ESP32 RTC maintains time across deep sleep — skip SNTP if already valid */
    if (time(NULL) > 1700000000LL) {
        ESP_LOGI(TAG, "Time already valid (RTC), skipping SNTP");
        return ESP_OK;
    }

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, APP_NTP_SERVER_1);
    esp_sntp_setservername(1, APP_NTP_SERVER_2);
    esp_sntp_init();

    const int max_waits = timeout_ms / 500;
    for (int i = 0; i < max_waits; ++i) {
        time_t now = time(NULL);
        if (now > 1700000000) {
            ESP_LOGI(TAG, "Time sync complete");
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGW(TAG, "Time sync timed out");
    return ESP_ERR_TIMEOUT;
}
