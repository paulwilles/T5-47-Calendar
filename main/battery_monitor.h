#ifndef BATTERY_MONITOR_H
#define BATTERY_MONITOR_H

#include <stddef.h>
#include "esp_err.h"

/**
 * Initialise ADC1 channel 0 (GPIO36 = BATT_PIN on LilyGo T5 4.7").
 * Call once at startup.  Returns ESP_OK on success.
 */
esp_err_t battery_monitor_init(void);

/**
 * Read the battery voltage.
 * Returns voltage in millivolts (already compensated for the 2:1 on-board
 * voltage divider), or 0 if the ADC is not initialised.
 */
int battery_monitor_read_mv(void);

/**
 * Format a short battery string into buf (e.g. "3.97V 86%").
 * buf must be at least 16 bytes.
 */
void battery_monitor_format(char *buf, size_t len);

#endif /* BATTERY_MONITOR_H */
