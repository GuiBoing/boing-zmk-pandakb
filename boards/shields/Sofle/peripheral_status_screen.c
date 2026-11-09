/*
 * Status screen for the right (peripheral) half of the Sofle, 128x32 OLED.
 *
 *   |[####  ] 85%                <>|   8px status bar (UNSCII 8)
 *   |  Hammerbeam art slideshow    |   128x24, changes every
 *   |  (art.c)                     |   CONFIG_SOFLE_ART_INTERVAL_MS
 *
 * Battery icon is drawn with plain objects (no symbol glyphs needed);
 * "<>" = linked to the left half, "--" = not linked.
 *
 * The slideshow runs on an LVGL timer, which only ticks while the display
 * is on, so it costs nothing once the screen blanks on idle.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>

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

#define BAR_H 8
#define BATT_W 14
#define BATT_H 7

extern const lv_img_dsc_t *sofle_art_imgs[];
extern const uint8_t sofle_art_count;

static lv_obj_t *art_img;
static uint8_t art_index;

static lv_obj_t *batt_fill;
static lv_obj_t *batt_label;
static lv_obj_t *link_label;

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

static void sofle_periph_update_cb(struct sofle_periph_state state) {
    char text[8];

    if (batt_fill != NULL) {
        /* inner width = BATT_W - 2 (border) - 2 (padding) */
        lv_coord_t w = ((BATT_W - 4) * state.level + 50) / 100;
        if (w < 1) {
            w = 1;
        }
        lv_obj_set_width(batt_fill, w);
    }

    if (batt_label != NULL) {
        snprintf(text, sizeof(text), "%u%%", state.level);
        lv_label_set_text(batt_label, text);
    }

    if (link_label != NULL) {
        lv_label_set_text(link_label, state.connected ? "<>" : "--");
    }
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

static lv_obj_t *plain_box(lv_obj_t *parent) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_text_font(screen, &lv_font_unscii_8, LV_PART_MAIN);
    /* draw the battery icon in the same color the theme uses for text */
    lv_color_t fg = lv_obj_get_style_text_color(screen, LV_PART_MAIN);

    /* battery outline */
    lv_obj_t *batt = plain_box(screen);
    lv_obj_set_size(batt, BATT_W, BATT_H);
    lv_obj_set_style_border_width(batt, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(batt, fg, LV_PART_MAIN);
    lv_obj_set_style_border_opa(batt, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(batt, LV_ALIGN_TOP_LEFT, 0, 0);

    /* battery tip */
    lv_obj_t *tip = plain_box(screen);
    lv_obj_set_size(tip, 1, 3);
    lv_obj_set_style_bg_color(tip, fg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(tip, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align_to(tip, batt, LV_ALIGN_OUT_RIGHT_MID, 0, 0);

    /* battery fill */
    batt_fill = plain_box(batt);
    lv_obj_set_size(batt_fill, 1, BATT_H - 4);
    lv_obj_set_style_bg_color(batt_fill, fg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(batt_fill, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_align(batt_fill, LV_ALIGN_LEFT_MID, 1, 0);

    batt_label = lv_label_create(screen);
    lv_obj_align(batt_label, LV_ALIGN_TOP_LEFT, BATT_W + 4, 0);

    link_label = lv_label_create(screen);
    lv_obj_align(link_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    /* art slideshow */
    art_index = 0;
    art_img = lv_img_create(screen);
    lv_img_set_src(art_img, sofle_art_imgs[art_index]);
    lv_obj_align(art_img, LV_ALIGN_TOP_LEFT, 0, BAR_H);
    lv_timer_create(art_timer_cb, CONFIG_SOFLE_ART_INTERVAL_MS, NULL);

    widget_sofle_periph_init();

    return screen;
}
