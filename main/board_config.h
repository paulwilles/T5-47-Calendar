#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_BUTTON_NONE (-1)

/*
 * Lilygo T5 4.7 ESP32 WROVER-E physical buttons:
 *   IO34 -> Previous  (prev day / prev item in detail)
 *   IO35 -> Next      (next day / next item in detail)
 *   IO39 -> Select    (enter / exit detail view)
 *   RST  -> Reset     (hardware reset, not used for navigation)
 *
 * NOTE: IO0 is CFG_STR (EPD strapping pin) in the vendor driver and
 *       cannot be used as a general-purpose GPIO input.
 */
#define BOARD_BUTTON_PREV_PIN   35
#define BOARD_BUTTON_NEXT_PIN   34
#define BOARD_BUTTON_SELECT_PIN 39
#define BOARD_BUTTON_HOME_PIN   BOARD_BUTTON_NONE

#define BOARD_BUTTON_COUNT 3

#endif
