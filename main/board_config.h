#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#define BOARD_BUTTON_NONE (-1)

/*
 * Lilygo T5 4.7 ESP32 WROVER-E reference pins from the vendor examples.
 * The board appears to expose three direct button GPIOs on the ESP32 build.
 * Two logical buttons are reserved in the UI for future expansion.
 */
#define BOARD_BUTTON_PREV_PIN   34
#define BOARD_BUTTON_NEXT_PIN   35
#define BOARD_BUTTON_SELECT_PIN 39
#define BOARD_BUTTON_UP_PIN     BOARD_BUTTON_NONE
#define BOARD_BUTTON_DOWN_PIN   BOARD_BUTTON_NONE

#define BOARD_BUTTON_COUNT 5

#endif
