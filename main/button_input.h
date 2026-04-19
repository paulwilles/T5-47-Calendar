#ifndef BUTTON_INPUT_H
#define BUTTON_INPUT_H

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    BUTTON_ACTION_NONE = 0,
    BUTTON_ACTION_PREV,
    BUTTON_ACTION_NEXT,
    BUTTON_ACTION_UP,
    BUTTON_ACTION_DOWN,
    BUTTON_ACTION_SELECT,
} button_action_t;

esp_err_t button_input_init(void);
button_action_t button_input_poll(void);
const char *button_input_name(button_action_t action);
bool button_input_is_available(button_action_t action);

#endif
