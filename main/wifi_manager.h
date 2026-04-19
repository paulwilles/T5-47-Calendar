#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

esp_err_t wifi_manager_init(void);
bool wifi_manager_is_connected(void);
bool wifi_manager_is_configured(void);
esp_err_t wifi_manager_wait_for_connection(int timeout_ms);
esp_err_t wifi_manager_sync_time(int timeout_ms);
const char *wifi_manager_status_text(void);

#endif
