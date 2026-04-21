#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#define APP_SCREEN_WIDTH 960
#define APP_SCREEN_HEIGHT 540
#define APP_TOP_BAR_HEIGHT 38
#define APP_LEFT_PANE_WIDTH (APP_SCREEN_WIDTH / 3)
#define APP_RIGHT_PANE_WIDTH (APP_SCREEN_WIDTH - APP_LEFT_PANE_WIDTH)

#define APP_MAX_DAYS 28
#define APP_MAX_SCHEDULE_ITEMS 12

#define APP_LABEL_LEN 24
#define APP_ID_LEN 24
#define APP_TITLE_LEN 64
#define APP_TIME_LEN 16
#define APP_LOCATION_LEN 48
#define APP_SOURCE_LEN 32
#define APP_DETAIL_LEN 160
#define APP_URL_LEN 96

#define HELPER_SERVICE_URL_DEFAULT "http://192.168.0.237:8080"
#define APP_HELPER_SNAPSHOT_ENDPOINT "/api/v1/snapshot"

/* Hardware bring-up toggles */
#define APP_ENABLE_WIFI 1
#define APP_ENABLE_ASCII_PREVIEW 0

/* Edit these values for your home network when ready to test on hardware. */
#define APP_WIFI_SSID "YOUR_WIFI_SSID"
#define APP_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define APP_TIME_ZONE "AEST-10AEDT-11,M10.1.0/2,M4.1.0/3"
#define APP_NTP_SERVER_1 "pool.ntp.org"
#define APP_NTP_SERVER_2 "time.nist.gov"

#if defined(__has_include)
  #if __has_include("app_config_local.h")
    #include "app_config_local.h"
  #endif
#endif

#endif
