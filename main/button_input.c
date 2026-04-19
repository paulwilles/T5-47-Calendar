#include "button_input.h"

#include "board_config.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "button_input";
static int64_t s_last_press_us = 0;

static const int s_button_pins[] = {
    BOARD_BUTTON_PREV_PIN,
    BOARD_BUTTON_NEXT_PIN,
    BOARD_BUTTON_UP_PIN,
    BOARD_BUTTON_DOWN_PIN,
    BOARD_BUTTON_SELECT_PIN,
};

static const button_action_t s_button_actions[] = {
    BUTTON_ACTION_PREV,
    BUTTON_ACTION_NEXT,
    BUTTON_ACTION_UP,
    BUTTON_ACTION_DOWN,
    BUTTON_ACTION_SELECT,
};

static const char *s_button_names[] = {
    "none",
    "prev",
    "next",
    "up",
    "down",
    "select",
};

static bool is_pin_valid(int pin)
{
    return pin >= 0;
}

esp_err_t button_input_init(void)
{
    for (size_t i = 0; i < sizeof(s_button_pins) / sizeof(s_button_pins[0]); ++i) {
        int pin = s_button_pins[i];
        if (!is_pin_valid(pin)) {
            continue;
        }

        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << pin,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&io_conf));
    }

    ESP_LOGI(TAG, "Button input ready for configured GPIOs");
    return ESP_OK;
}

const char *button_input_name(button_action_t action)
{
    if (action < BUTTON_ACTION_NONE || action > BUTTON_ACTION_SELECT) {
        return "unknown";
    }
    return s_button_names[action];
}

bool button_input_is_available(button_action_t action)
{
    switch (action) {
        case BUTTON_ACTION_PREV: return is_pin_valid(BOARD_BUTTON_PREV_PIN);
        case BUTTON_ACTION_NEXT: return is_pin_valid(BOARD_BUTTON_NEXT_PIN);
        case BUTTON_ACTION_UP: return is_pin_valid(BOARD_BUTTON_UP_PIN);
        case BUTTON_ACTION_DOWN: return is_pin_valid(BOARD_BUTTON_DOWN_PIN);
        case BUTTON_ACTION_SELECT: return is_pin_valid(BOARD_BUTTON_SELECT_PIN);
        default: return false;
    }
}

button_action_t button_input_poll(void)
{
    int64_t now = esp_timer_get_time();
    if ((now - s_last_press_us) < 180000) {
        return BUTTON_ACTION_NONE;
    }

    for (size_t i = 0; i < sizeof(s_button_pins) / sizeof(s_button_pins[0]); ++i) {
        int pin = s_button_pins[i];
        if (!is_pin_valid(pin)) {
            continue;
        }

        if (gpio_get_level((gpio_num_t)pin) == 0) {
            s_last_press_us = now;
            ESP_LOGI(TAG, "Detected button: %s", button_input_name(s_button_actions[i]));
            return s_button_actions[i];
        }
    }

    return BUTTON_ACTION_NONE;
}
