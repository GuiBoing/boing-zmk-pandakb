/*
 * Status screen for the right (peripheral) half of the Sofle.
 *
 * The 128x32 OLED is mounted vertically, so the layout is designed in
 * portrait (32 wide x 128 tall) and rotated 90 deg CCW into the panel's
 * native landscape orientation (portrait top -> native left):
 *
 *   +--------+
 *   |[###]  v|  8px status: battery icon + check (linked to the left half)
 *   |        |               or X (not linked)
 *   |  art   |  32x66 Hammerbeam art (art.c, already pre-rotated),
 *   |        |  centered, changes every CONFIG_SOFLE_ART_INTERVAL_MS
 *   +--------+
 *
 * Battery % is not shown here: the left OLED already shows both halves.
 *
 * The status area is drawn on a hidden portrait canvas and copied pixel by
 * pixel, rotated, into the visible native canvas. If everything shows up
 * upside down, flip the panel in hardware (segment-remap/com-invdir in
 * Sofle_R_native.overlay).
 *
 * The slideshow runs on an LVGL timer, which only ticks while the display
 * is on, so it costs nothing once the screen blanks on idle.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <lvgl.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>

/* portrait geometry */
#define SCREEN_H 128
#define STATUS_W 32
#define STATUS_H 8
#define ART_W 32
#define ART_H 66

#define BATT_W 14
#define BATT_H 7

#define ICON_SIZE 7

extern const lv_img_dsc_t *sofle_art_imgs[];
extern const uint8_t sofle_art_count;

/* 7x7 icons, one byte per row, MSB = leftmost of 7 pixels (bit 6) */
static const uint8_t icon_check[ICON_SIZE] = {
    0x00, /* ....... */
    0x01, /* ......# */
    0x02, /* .....#. */
    0x04, /* ....#.. */
    0x48, /* #..#... */
    0x30, /* .##.... */
    0x00, /* ....... */
};

static const uint8_t icon_x[ICON_SIZE] = {
    0x41, /* #.....# */
    0x22, /* .#...#. */
    0x14, /* ..#.#.. */
    0x08, /* ...#... */
    0x14, /* ..#.#.. */
    0x22, /* .#...#. */
    0x41, /* #.....# */
};

static lv_obj_t *art_img;
static uint8_t art_index;

static lv_color_t fg;
static lv_color_t bg;

/* hidden portrait canvas (32x8) and visible native canvas (8x32) */
static lv_obj_t *status_portrait;
static lv_obj_t *status_native;
static lv_color_t portrait_buf[STATUS_W * STATUS_H];
static lv_color_t native_buf[STATUS_H * STATUS_W];

/* ---- slideshow ---- */

static void art_timer_cb(lv_timer_t *timer) {
    art_index = (art_index + 1) % sofle_art_count;
    lv_img_set_src(art_img, sofle_art_imgs[art_index]);
}

/* ---- battery + link status ---- */

struct sofle_periph_state {
    uint8_t level;
    bool connected;
};

static void draw_rect(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h,
                      lv_color_t color) {
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = color;
    dsc.bg_opa = LV_OPA_COVER;
    lv_canvas_draw_rect(canvas, x, y, w, h, &dsc);
}

static void draw_icon(lv_obj_t *canvas, lv_coord_t x0, lv_coord_t y0, const uint8_t *icon) {
    for (lv_coord_t y = 0; y < ICON_SIZE; y++) {
        for (lv_coord_t x = 0; x < ICON_SIZE; x++) {
            if (icon[y] & (1 << (ICON_SIZE - 1 - x))) {
                lv_canvas_set_px_color(canvas, x0 + x, y0 + y, fg);
            }
        }
    }
}

static void sofle_periph_update_cb(struct sofle_periph_state state) {
    if (status_portrait == NULL || status_native == NULL) {
        return;
    }

    lv_canvas_fill_bg(status_portrait, bg, LV_OPA_COVER);

    /* battery: outline, inner clear, fill, tip */
    draw_rect(status_portrait, 0, 0, BATT_W, BATT_H, fg);
    draw_rect(status_portrait, 1, 1, BATT_W - 2, BATT_H - 2, bg);
    lv_coord_t fill = ((BATT_W - 4) * state.level + 50) / 100;
    if (fill > 0) {
        draw_rect(status_portrait, 2, 2, fill, BATT_H - 4, fg);
    }
    draw_rect(status_portrait, BATT_W, 2, 1, 3, fg);

    /* link to the left half */
    draw_icon(status_portrait, STATUS_W - ICON_SIZE, 0, state.connected ? icon_check : icon_x);

    /* rotate 90 deg CCW: native(x, y) = portrait(STATUS_W - 1 - y, x) */
    for (lv_coord_t y = 0; y < STATUS_W; y++) {
        for (lv_coord_t x = 0; x < STATUS_H; x++) {
            lv_color_t c = lv_canvas_get_px(status_portrait, STATUS_W - 1 - y, x);
            lv_canvas_set_px_color(status_native, x, y, c);
        }
    }
    lv_obj_invalidate(status_native);
}

static struct sofle_periph_state sofle_periph_get_state(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = (eh != NULL) ? as_zmk_battery_state_changed(eh)
                                                              : NULL;

    return (struct sofle_periph_state){
        .level = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
        .connected = zmk_split_bt_peripheral_is_connected(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_sofle_periph, struct sofle_periph_state,
                            sofle_periph_update_cb, sofle_periph_get_state)

ZMK_SUBSCRIPTION(widget_sofle_periph, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(widget_sofle_periph, zmk_split_peripheral_status_changed);

/* ---- screen ---- */

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);

    /* use the theme's colors so icons match the rest of the UI */
    fg = lv_obj_get_style_text_color(screen, LV_PART_MAIN);
    bg = lv_obj_get_style_bg_color(screen, LV_PART_MAIN);

    status_portrait = lv_canvas_create(screen);
    lv_canvas_set_buffer(status_portrait, portrait_buf, STATUS_W, STATUS_H,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_add_flag(status_portrait, LV_OBJ_FLAG_HIDDEN);

    status_native = lv_canvas_create(screen);
    lv_canvas_set_buffer(status_native, native_buf, STATUS_H, STATUS_W, LV_IMG_CF_TRUE_COLOR);
    lv_obj_align(status_native, LV_ALIGN_TOP_LEFT, 0, 0);

    /* art slideshow: native 66x32, centered in the space below the status */
    art_index = 0;
    art_img = lv_img_create(screen);
    lv_img_set_src(art_img, sofle_art_imgs[art_index]);
    lv_obj_align(art_img, LV_ALIGN_TOP_LEFT,
                 STATUS_H + (SCREEN_H - STATUS_H - ART_H) / 2, 0);
    lv_timer_create(art_timer_cb, CONFIG_SOFLE_ART_INTERVAL_MS, NULL);

    widget_sofle_periph_init();

    return screen;
}
