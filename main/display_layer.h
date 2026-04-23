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
    const char *datetime_str;
    bool full_refresh;   /* true = epd_clear() before draw (data changed); false = fast redraw (navigation only) */
} display_render_request_t;

esp_err_t display_layer_init(bool skip_splash);
void display_layer_render(const display_render_request_t *request);
void display_layer_update_topbar(const char *datetime_str, const char *wifi_status, bool detail_mode, bool sleep_mode);
/* Show a fast nav hint (< or > symbols next to the "4 Week View" title) without a full render.
 * count=0 clears the hint back to plain "4 Week View". forward=true means NEXT (>). */
void display_layer_show_nav_hint(int count, bool forward);
uint32_t display_layer_framebuffer_checksum(void);

#endif
