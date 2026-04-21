#ifndef DISPLAY_LAYER_H
#define DISPLAY_LAYER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "helper_service.h"

typedef struct {
    const helper_snapshot_t *snapshot;
    int selected_day_index;
    int selected_item_index;
    bool detail_mode;
    const char *wifi_status;
    bool full_refresh;   /* true = epd_clear() before draw (data changed); false = fast redraw (navigation only) */
} display_render_request_t;

esp_err_t display_layer_init(void);
void display_layer_render(const display_render_request_t *request);
uint32_t display_layer_framebuffer_checksum(void);

#endif
