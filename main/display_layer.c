#include "display_layer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_config.h"
#include "epd_driver.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "firasans.h"

static const char *TAG = "display_layer";

#define COLOR_BLACK 0x0
#define COLOR_DARK  0x3
#define COLOR_MID   0x7
#define COLOR_LIGHT 0xC
#define COLOR_WHITE 0xF

/* Clock region constants — must match the position used in build_top_bar() */
#define CLOCK_X      (APP_SCREEN_WIDTH - 262)
#define CLOCK_Y      8
#define CLOCK_W      256   /* 21 chars * 12px + margin, kept even for EPD alignment */
#define CLOCK_H      28    /* covers text rows y=12..y=26 with margin */

static uint8_t *s_framebuffer = NULL;
static size_t s_framebuffer_size = 0;
static bool s_epd_ready = false;
static int s_fast_refresh_count = 0;
#define FULL_REFRESH_EVERY_N_FAST 10   /* force a full clear after this many fast redraws to prevent ghosting */

static void set_pixel(int x, int y, uint8_t shade)
{
    if (!s_framebuffer || x < 0 || y < 0 || x >= APP_SCREEN_WIDTH || y >= APP_SCREEN_HEIGHT) {
        return;
    }

    shade &= 0x0F;
    size_t index = ((size_t)y * APP_SCREEN_WIDTH + (size_t)x) / 2;
    if ((x & 1) == 0) {
        s_framebuffer[index] = (uint8_t)((s_framebuffer[index] & 0xF0) | shade);
    } else {
        s_framebuffer[index] = (uint8_t)((s_framebuffer[index] & 0x0F) | (shade << 4));
    }
}

static uint8_t get_pixel(int x, int y)
{
    if (!s_framebuffer || x < 0 || y < 0 || x >= APP_SCREEN_WIDTH || y >= APP_SCREEN_HEIGHT) {
        return COLOR_WHITE;
    }

    size_t index = ((size_t)y * APP_SCREEN_WIDTH + (size_t)x) / 2;
    return (x & 1) == 0 ? (uint8_t)(s_framebuffer[index] & 0x0F) : (uint8_t)(s_framebuffer[index] >> 4);
}

static void fill_rect(int x, int y, int w, int h, uint8_t shade)
{
    for (int yy = y; yy < y + h; ++yy) {
        for (int xx = x; xx < x + w; ++xx) {
            set_pixel(xx, yy, shade);
        }
    }
}

static void draw_rect(int x, int y, int w, int h, uint8_t shade)
{
    for (int xx = x; xx < x + w; ++xx) {
        set_pixel(xx, y, shade);
        set_pixel(xx, y + h - 1, shade);
    }
    for (int yy = y; yy < y + h; ++yy) {
        set_pixel(x, yy, shade);
        set_pixel(x + w - 1, yy, shade);
    }
}

static void draw_vline(int x, int y, int h, uint8_t shade)
{
    for (int yy = y; yy < y + h; ++yy) {
        set_pixel(x, yy, shade);
    }
}

static void draw_hline(int x, int y, int w, uint8_t shade)
{
    for (int xx = x; xx < x + w; ++xx) {
        set_pixel(xx, y, shade);
    }
}

static const uint8_t *glyph_for_char(char c)
{
    static const uint8_t A[5] = {0x7, 0x5, 0x7, 0x5, 0x5};
    static const uint8_t B[5] = {0x6, 0x5, 0x6, 0x5, 0x6};
    static const uint8_t C[5] = {0x7, 0x4, 0x4, 0x4, 0x7};
    static const uint8_t D[5] = {0x6, 0x5, 0x5, 0x5, 0x6};
    static const uint8_t E[5] = {0x7, 0x4, 0x6, 0x4, 0x7};
    static const uint8_t F[5] = {0x7, 0x4, 0x6, 0x4, 0x4};
    static const uint8_t G[5] = {0x7, 0x4, 0x5, 0x5, 0x7};
    static const uint8_t H[5] = {0x5, 0x5, 0x7, 0x5, 0x5};
    static const uint8_t I[5] = {0x7, 0x2, 0x2, 0x2, 0x7};
    static const uint8_t J[5] = {0x1, 0x1, 0x1, 0x5, 0x7};
    static const uint8_t K[5] = {0x5, 0x5, 0x6, 0x5, 0x5};
    static const uint8_t L[5] = {0x4, 0x4, 0x4, 0x4, 0x7};
    static const uint8_t M[5] = {0x5, 0x7, 0x7, 0x5, 0x5};
    static const uint8_t N[5] = {0x5, 0x7, 0x7, 0x7, 0x5};
    static const uint8_t O[5] = {0x7, 0x5, 0x5, 0x5, 0x7};
    static const uint8_t P[5] = {0x7, 0x5, 0x7, 0x4, 0x4};
    static const uint8_t Q[5] = {0x7, 0x5, 0x5, 0x7, 0x1};
    static const uint8_t R[5] = {0x7, 0x5, 0x7, 0x6, 0x5};
    static const uint8_t S[5] = {0x7, 0x4, 0x7, 0x1, 0x7};
    static const uint8_t T[5] = {0x7, 0x2, 0x2, 0x2, 0x2};
    static const uint8_t U[5] = {0x5, 0x5, 0x5, 0x5, 0x7};
    static const uint8_t V[5] = {0x5, 0x5, 0x5, 0x5, 0x2};
    static const uint8_t W[5] = {0x5, 0x5, 0x7, 0x7, 0x5};
    static const uint8_t X[5] = {0x5, 0x5, 0x2, 0x5, 0x5};
    static const uint8_t Y[5] = {0x5, 0x5, 0x2, 0x2, 0x2};
    static const uint8_t Z[5] = {0x7, 0x1, 0x2, 0x4, 0x7};
    static const uint8_t N0[5] = {0x7, 0x5, 0x5, 0x5, 0x7};
    static const uint8_t N1[5] = {0x2, 0x6, 0x2, 0x2, 0x7};
    static const uint8_t N2[5] = {0x7, 0x1, 0x7, 0x4, 0x7};
    static const uint8_t N3[5] = {0x7, 0x1, 0x7, 0x1, 0x7};
    static const uint8_t N4[5] = {0x5, 0x5, 0x7, 0x1, 0x1};
    static const uint8_t N5[5] = {0x7, 0x4, 0x7, 0x1, 0x7};
    static const uint8_t N6[5] = {0x7, 0x4, 0x7, 0x5, 0x7};
    static const uint8_t N7[5] = {0x7, 0x1, 0x1, 0x2, 0x2};
    static const uint8_t N8[5] = {0x7, 0x5, 0x7, 0x5, 0x7};
    static const uint8_t N9[5] = {0x7, 0x5, 0x7, 0x1, 0x7};
    static const uint8_t DASH[5] = {0x0, 0x0, 0x7, 0x0, 0x0};
    static const uint8_t COLON[5] = {0x0, 0x2, 0x0, 0x2, 0x0};
    static const uint8_t SLASH[5] = {0x1, 0x1, 0x2, 0x4, 0x4};
    static const uint8_t SPACE[5] = {0x0, 0x0, 0x0, 0x0, 0x0};

    c = (char)toupper((unsigned char)c);
    switch (c) {
        case 'A': return A; case 'B': return B; case 'C': return C; case 'D': return D;
        case 'E': return E; case 'F': return F; case 'G': return G; case 'H': return H;
        case 'I': return I; case 'J': return J; case 'K': return K; case 'L': return L;
        case 'M': return M; case 'N': return N; case 'O': return O; case 'P': return P;
        case 'Q': return Q; case 'R': return R; case 'S': return S; case 'T': return T;
        case 'U': return U; case 'V': return V; case 'W': return W; case 'X': return X;
        case 'Y': return Y; case 'Z': return Z;
        case '0': return N0; case '1': return N1; case '2': return N2; case '3': return N3;
        case '4': return N4; case '5': return N5; case '6': return N6; case '7': return N7;
        case '8': return N8; case '9': return N9;
        case '-': return DASH; case ':': return COLON; case '/': return SLASH; case ' ': return SPACE;
        default: return SPACE;
    }
}

static void draw_char(int x, int y, char c, uint8_t shade, int scale)
{
    const uint8_t *glyph = glyph_for_char(c);
    for (int row = 0; row < 5; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (glyph[row] & (1 << (2 - col))) {
                fill_rect(x + (col * scale), y + (row * scale), scale, scale, shade);
            }
        }
    }
}

static void draw_text(int x, int y, const char *text, uint8_t shade, int scale, int max_chars)
{
    int cursor_x = x;
    for (int i = 0; text && text[i] != '\0' && i < max_chars; ++i) {
        draw_char(cursor_x, y, text[i], shade, scale);
        cursor_x += 4 * scale;
    }
}

static void build_top_bar(const display_render_request_t *request)
{
    fill_rect(0, 0, APP_SCREEN_WIDTH, APP_TOP_BAR_HEIGHT, COLOR_DARK);
    draw_rect(0, 0, APP_SCREEN_WIDTH, APP_TOP_BAR_HEIGHT, COLOR_BLACK);
    if (request->wifi_status) {
        draw_text(12, 12, request->wifi_status, COLOR_WHITE, 3, 16);
    }
    draw_text(225, 12, "RST  BOOT  PREV  NEXT  SEL", COLOR_WHITE, 3, 28);
    if (request->datetime_str) {
        draw_text(CLOCK_X, 12, request->datetime_str, COLOR_WHITE, 3, 21);
    }
}

/* --- FiraSans helpers ---------------------------------------------------- */

/* Draw one line of FiraSans text. top_y is the top of the text cell;     *
 * we add the ascender internally to get the baseline.                     */
static void draw_fira(int x, int top_y, const char *text)
{
    if (!text || !text[0] || !s_framebuffer) return;
    int32_t cx = x;
    int32_t cy = top_y + FiraSans.ascender;
    writeln(&FiraSans, text, &cx, &cy, s_framebuffer);
}

/* Draw FiraSans text with word-wrap.  Wraps at word boundaries to fit      *
 * within max_w pixels.  max_lines <= 0 means unlimited.                   *
 * Returns the y coordinate of the row after the last drawn line.          */
static int draw_fira_wrapped(int x, int top_y, const char *text, int max_w, int max_lines)
{
    if (!text || !text[0] || !s_framebuffer) return top_y;
    char buf[256];
    strncpy(buf, text, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    int line = 0;
    char *head = buf;
    const int line_h = (int)FiraSans.advance_y;

    while (*head && (max_lines <= 0 || line < max_lines)) {
        char *best = head;   /* last word boundary that still fits */
        char *scan = head;
        while (*scan) {
            char *wend = scan;
            while (*wend && *wend != ' ') wend++;
            char sv = *wend; *wend = '\0';
            int32_t tx = x, ty = 0, x1, y1, w, h;
            get_text_bounds(&FiraSans, head, &tx, &ty, &x1, &y1, &w, &h, NULL);
            *wend = sv;
            if (w > max_w) {
                if (best == head) best = wend; /* single long word: force-break */
                break;
            }
            best = wend;
            if (!sv) break;
            scan = wend + 1;
        }
        char sv = *best; *best = '\0';
        draw_fira(x, top_y + line * line_h, head);
        *best = sv;
        line++;
        head = best;
        if (*head == ' ') head++;
    }
    return top_y + line * line_h;
}

static void build_overview_grid(const display_render_request_t *request)
{
    int origin_x = APP_LEFT_PANE_WIDTH + 8;
    int origin_y = APP_TOP_BAR_HEIGHT + 28;
    int grid_w = APP_RIGHT_PANE_WIDTH - 16;
    int grid_h = APP_SCREEN_HEIGHT - APP_TOP_BAR_HEIGHT - 36;
    int cell_w = grid_w / 7;
    int cell_h = grid_h / 4;

    draw_text(origin_x, APP_TOP_BAR_HEIGHT + 8, request->detail_mode ? "DETAIL" : "4 WEEK VIEW", COLOR_BLACK, 3, 20);
    draw_rect(origin_x, origin_y, grid_w, grid_h, COLOR_DARK);

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 7; ++col) {
            int index = (row * 7) + col;
            int x = origin_x + (col * cell_w);
            int y = origin_y + (row * cell_h);
            bool selected = (index == request->selected_day_index);
            draw_rect(x, y, cell_w, cell_h, selected ? COLOR_BLACK : COLOR_MID);

            if (!request->snapshot || index >= request->snapshot->day_count) {
                continue;
            }

            const overview_day_t *day = &request->snapshot->overview[index];
            if (selected) {
                fill_rect(x + 1, y + 1, cell_w - 2, cell_h - 2, COLOR_LIGHT);
            }
            draw_text(x + 8, y + 8, day->weekday, COLOR_BLACK, 2, 8);
            char num[8];
            snprintf(num, sizeof(num), "%u", day->day_of_month);
            draw_text(x + 8, y + 28, num, COLOR_BLACK, 3, 4);
            if (day->item_count > 0) {
                fill_rect(x + cell_w - 18, y + 8, 10, 10, COLOR_DARK);
            }
        }
    }
}

static void build_left_agenda(const display_render_request_t *request)
{
    int pane_x = 8;
    int pane_y = APP_TOP_BAR_HEIGHT + 8;
    int pane_w = APP_LEFT_PANE_WIDTH - 12;
    int pane_h = APP_SCREEN_HEIGHT - APP_TOP_BAR_HEIGHT - 16;

    draw_text(pane_x + 8, pane_y + 6, "AGENDA", COLOR_BLACK, 3, 16);
    draw_rect(pane_x, pane_y + 24, pane_w, pane_h - 24, COLOR_DARK);

    if (!request->snapshot) {
        return;
    }

    const day_schedule_t *schedule = helper_service_get_day(request->snapshot, request->selected_day_index);
    if (!schedule) {
        return;
    }

    draw_text(pane_x + 8, pane_y + 32, schedule->day_label, COLOR_BLACK, 2, 24);
    int item_y = pane_y + 60;
    /* Each item box: 12px time label + 4px gap + FiraSans title (~51px) + 5px pad = 72px; step 76px */
    const int ITEM_H = 72;
    const int ITEM_STEP = 76;

    for (int i = 0; i < schedule->item_count && i < 5; ++i) {
        const calendar_item_t *item = &schedule->items[i];
        bool selected = (i == request->selected_item_index);
        char time_text[32] = {0};
        if (item->all_day || strcmp(item->start_label, "Any") == 0 || item->start_label[0] == '\0') {
            snprintf(time_text, sizeof(time_text), "ALL DAY");
        } else if (item->end_label[0] != '\0') {
            snprintf(time_text, sizeof(time_text), "%s-%s", item->start_label, item->end_label);
        } else {
            snprintf(time_text, sizeof(time_text), "%s", item->start_label);
        }

        fill_rect(pane_x + 8, item_y, pane_w - 16, ITEM_H, selected ? COLOR_LIGHT : COLOR_WHITE);
        draw_rect(pane_x + 8, item_y, pane_w - 16, ITEM_H, selected ? COLOR_BLACK : COLOR_MID);
        draw_text(pane_x + 14, item_y + 5, time_text, COLOR_BLACK, 2, 16);
        draw_fira(pane_x + 14, item_y + 20, item->title);
        item_y += ITEM_STEP;
    }
}

static void build_detail_panel(const display_render_request_t *request)
{
    if (!request->detail_mode || !request->snapshot) {
        return;
    }

    const calendar_item_t *item = helper_service_get_item(request->snapshot,
                                                          request->selected_day_index,
                                                          request->selected_item_index);
    if (!item) {
        return;
    }

    int x = APP_LEFT_PANE_WIDTH + 16;
    int y = APP_TOP_BAR_HEIGHT + 44;
    int w = APP_RIGHT_PANE_WIDTH - 32;
    int h = APP_SCREEN_HEIGHT - APP_TOP_BAR_HEIGHT - 56;
    char time_text[32] = {0};

    if (item->all_day || strcmp(item->start_label, "Any") == 0 || item->start_label[0] == '\0') {
        snprintf(time_text, sizeof(time_text), "ALL DAY");
    } else if (item->end_label[0] != '\0') {
        snprintf(time_text, sizeof(time_text), "%s-%s", item->start_label, item->end_label);
    } else {
        snprintf(time_text, sizeof(time_text), "%s", item->start_label);
    }

    fill_rect(x, y, w, h, COLOR_WHITE);
    draw_rect(x, y, w, h, COLOR_BLACK);

    /* Title — word-wrapped, up to 2 lines, no truncation */
    int field_y = draw_fira_wrapped(x + 12, y + 12, item->title, w - 24, 2) + 12;

    /* Horizontal rule below title */
    draw_hline(x + 12, field_y - 4, w - 24, COLOR_MID);

    /* Fields: small block-font label on the left, FiraSans value on the right */
    const int LABEL_W = 100;
    const int ROW_H = (int)FiraSans.advance_y + 4;

    draw_text(x + 12, field_y + 2,        "TIME",   COLOR_DARK, 2, 8);
    draw_fira(x + 12 + LABEL_W, field_y,  time_text);
    field_y += ROW_H;

    draw_text(x + 12, field_y + 2,        "SOURCE", COLOR_DARK, 2, 8);
    draw_fira(x + 12 + LABEL_W, field_y,  item->source[0]   ? item->source   : "None");
    field_y += ROW_H;

    draw_text(x + 12, field_y + 2,        "PLACE",  COLOR_DARK, 2, 8);
    draw_fira(x + 12 + LABEL_W, field_y,  item->location[0] ? item->location : "None");
    field_y += ROW_H;

    draw_text(x + 12, field_y + 2,        "NOTES",  COLOR_DARK, 2, 8);
    draw_fira_wrapped(x + 12 + LABEL_W, field_y, item->detail[0] ? item->detail : "None",
                      w - 24 - LABEL_W, 3);
}

static void dump_ascii_preview(void)
{
#if APP_ENABLE_ASCII_PREVIEW
    static const char shades[] = " .:-=+*#%@";
    const int cols = 80;
    const int rows = 24;
    const int step_x = APP_SCREEN_WIDTH / cols;
    const int step_y = APP_SCREEN_HEIGHT / rows;

    ESP_LOGI(TAG, "ASCII PREVIEW START");
    for (int row = 0; row < rows; ++row) {
        char line[cols + 1];
        for (int col = 0; col < cols; ++col) {
            int sx = col * step_x;
            int sy = row * step_y;
            uint8_t px = get_pixel(sx, sy);
            int idx = (px * 9) / 15;
            if (idx < 0) idx = 0;
            if (idx > 9) idx = 9;
            line[col] = shades[idx];
        }
        line[cols] = '\0';
        ESP_LOGI(TAG, "%s", line);
    }
    ESP_LOGI(TAG, "ASCII PREVIEW END");
#endif
}

esp_err_t display_layer_init(void)
{
    s_framebuffer_size = (APP_SCREEN_WIDTH * APP_SCREEN_HEIGHT) / 2;
    s_framebuffer = (uint8_t *)heap_caps_malloc(s_framebuffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_framebuffer) {
        s_framebuffer = (uint8_t *)heap_caps_malloc(s_framebuffer_size, MALLOC_CAP_8BIT);
    }
    if (!s_framebuffer) {
        return ESP_ERR_NO_MEM;
    }

    memset(s_framebuffer, 0xFF, s_framebuffer_size);
    ESP_LOGI(TAG, "Display framebuffer allocated: %u bytes", (unsigned)s_framebuffer_size);

    /* Splash screen: light background so the epd_clear() before the first calendar
     * render produces no visible flash (clear-to-white on a white screen is invisible). */
    fill_rect(0, 0, APP_SCREEN_WIDTH, APP_SCREEN_HEIGHT, COLOR_WHITE);
    fill_rect(0, 0, APP_SCREEN_WIDTH, 6, COLOR_BLACK);
    fill_rect(0, APP_SCREEN_HEIGHT - 6, APP_SCREEN_WIDTH, 6, COLOR_BLACK);
    draw_text(APP_SCREEN_WIDTH / 2 - 120, APP_SCREEN_HEIGHT / 2 - 30, "T5 Calendar", COLOR_BLACK, 4, 20);
    draw_text(APP_SCREEN_WIDTH / 2 - 80, APP_SCREEN_HEIGHT / 2 + 10,  "Connecting...", COLOR_MID, 3, 16);

    ESP_LOGI(TAG, "Initializing physical E-paper panel");
    epd_init();
    s_epd_ready = true;

    epd_poweron();
    epd_clear();
    epd_draw_grayscale_image(epd_full_screen(), s_framebuffer);
    epd_poweroff();
    ESP_LOGI(TAG, "Boot test screen pushed to physical panel");
    return ESP_OK;
}

void display_layer_update_clock(const char *datetime_str)
{
    if (!s_framebuffer || !s_epd_ready || !datetime_str) {
        return;
    }

    /* Repaint the clock slot in the framebuffer (dark bg + white text) */
    fill_rect(CLOCK_X, CLOCK_Y, CLOCK_W, CLOCK_H, COLOR_DARK);
    draw_text(CLOCK_X, 12, datetime_str, COLOR_WHITE, 3, 21);

    /* Push only the clock rect to the EPD — no epd_clear(), so no screen flash */
    Rect_t clock_rect = { .x = CLOCK_X, .y = CLOCK_Y, .width = CLOCK_W, .height = CLOCK_H };
    ESP_LOGI(TAG, "Clock partial update: %s", datetime_str);
    epd_poweron();
    epd_draw_grayscale_image(clock_rect, s_framebuffer);
    epd_poweroff();
}

void display_layer_render(const display_render_request_t *request)
{
    if (!s_framebuffer || !request) {
        return;
    }

    memset(s_framebuffer, 0xFF, s_framebuffer_size);
    build_top_bar(request);
    build_left_agenda(request);
    build_overview_grid(request);
    build_detail_panel(request);
    dump_ascii_preview();

    if (s_epd_ready) {
        bool do_clear = request->full_refresh;
        if (!do_clear) {
            s_fast_refresh_count++;
            if (s_fast_refresh_count >= FULL_REFRESH_EVERY_N_FAST) {
                do_clear = true;
                ESP_LOGI(TAG, "Periodic full clear to reset ghosting (after %d fast refreshes)", s_fast_refresh_count);
            }
        }
        if (do_clear) {
            s_fast_refresh_count = 0;
        }
        ESP_LOGI(TAG, "Starting E-paper refresh (type=%s)", do_clear ? "full" : "fast");
        epd_poweron();
        if (do_clear) {
            epd_clear();
        }
        epd_draw_grayscale_image(epd_full_screen(), s_framebuffer);
        epd_poweroff();
        ESP_LOGI(TAG, "E-paper refresh complete");
    }
}

uint32_t display_layer_framebuffer_checksum(void)
{
    uint32_t checksum = 2166136261u;
    for (size_t i = 0; i < s_framebuffer_size; ++i) {
        checksum ^= s_framebuffer[i];
        checksum *= 16777619u;
    }
    return checksum;
}
