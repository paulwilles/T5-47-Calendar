#ifndef BOARD_HAS_PSRAM
#error "Please enable PSRAM !!!"
#endif

#include <Arduino.h>
#include "epd_driver.h"
#include "utilities.h"
#include "firasans.h"

RTC_DATA_ATTR int boot_count{ 0 };
uint8_t *framebuffer = NULL;

void setup()
{
    Serial.begin(115200);
    delay(1000);
    framebuffer = (uint8_t *)ps_calloc(sizeof(uint8_t), EPD_WIDTH * EPD_HEIGHT / 2);
    if (!framebuffer) {
        Serial.println("alloc memory failed !!!");
        while (1);
    }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    ++boot_count;
    Serial.println("\nBoot #" + String(boot_count) + ".");

    Serial.println("epd_init()");
    epd_init();

    Serial.println("epd_poweron()");
    epd_poweron();
    epd_clear();

    int32_t cursor_x = 200;
    int32_t cursor_y = 200;

    String str = "➸ Boot Count:" + String(boot_count);
    writeln((GFXfont *)&FiraSans, str.c_str(), &cursor_x, &cursor_y, NULL);

    epd_poweroff_all();
    Serial.println("epd_poweroff()");


    Serial.end();
    delay(3000);

    esp_sleep_enable_timer_wakeup( 10 * 1000 * 1000 ); // microseconds
    esp_deep_sleep_start();
}

void loop()
{
    // This loop is never reached. The ESP32 will go into deep sleep before it would have reached this loop.
}