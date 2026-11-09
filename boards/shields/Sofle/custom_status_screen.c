/*
 * Status screen for the left (central) half of the Sofle, 128x32 OLED.
 *
 *   | <output status>          L 85% |
 *   | <layer>                  R 90% |
 *
 * Same layout as ZMK's built-in screen, but the battery widget shows both
 * halves (right half comes from CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
 * and the WPM widget is gone (it redraws constantly and costs battery).
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <lvgl.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/usb.h>
#include <zmk/events/usb_conn_state_changed.h>

#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
#include <zmk/display/widgets/output_status.h>
static struct zmk_widget_output_status output_status_widget;
#endif

#if IS_ENABLED(CONFIG_ZMK_WIDGET_LAYER_STATUS)
#include <zmk/display/widgets/layer_status.h>
static struct zmk_widget_layer_status layer_status_widget;
#endif

#define PERIPHERAL_UNKNOWN 0xFF

static lv_obj_t *central_label;
static lv_obj_t *peripheral_label;

/* Last known right-half level, updated only from events (0 = disconnected). */
static uint8_t peripheral_level = PERIPHERAL_UNKNOWN;

struct sofle_battery_state {
    uint8_t central;
    uint8_t peripheral;
    bool usb_present;
};

static void sofle_battery_update_cb(struct sofle_battery_state state) {
    char text[16];

    if (central_label != NULL) {
        snprintf(text, sizeof(text), "%sL %3u%%", state.usb_present ? LV_SYMBOL_CHARGE : "",
                 state.central);
        lv_label_set_text(central_label, text);
    }

    if (peripheral_label != NULL) {
        if (state.peripheral == PERIPHERAL_UNKNOWN || state.peripheral == 0) {
            lv_label_set_text(peripheral_label, "R  --");
        } else {
            snprintf(text, sizeof(text), "R %3u%%", state.peripheral);
            lv_label_set_text(peripheral_label, text);
        }
    }
}

static struct sofle_battery_state sofle_battery_get_state(const zmk_event_t *eh) {
    if (eh != NULL) {
        const struct zmk_peripheral_battery_state_changed *pev =
            as_zmk_peripheral_battery_state_changed(eh);
        if (pev != NULL && pev->source == 0) {
            peripheral_level = pev->state_of_charge;
        }
    }

    const struct zmk_battery_state_changed *ev = (eh != NULL) ? as_zmk_battery_state_changed(eh)
                                                              : NULL;

    return (struct sofle_battery_state){
        .central = (ev != NULL) ? ev->state_of_charge : zmk_battery_state_of_charge(),
        .peripheral = peripheral_level,
        .usb_present = zmk_usb_is_powered(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_sofle_battery, struct sofle_battery_state,
                            sofle_battery_update_cb, sofle_battery_get_state)

ZMK_SUBSCRIPTION(widget_sofle_battery, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(widget_sofle_battery, zmk_peripheral_battery_state_changed);
ZMK_SUBSCRIPTION(widget_sofle_battery, zmk_usb_conn_state_changed);

lv_obj_t *zmk_display_status_screen() {
    lv_obj_t *screen = lv_obj_create(NULL);

#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_TOP_LEFT, 0, 0);
#endif

#if IS_ENABLED(CONFIG_ZMK_WIDGET_LAYER_STATUS)
    zmk_widget_layer_status_init(&layer_status_widget, screen);
    lv_obj_set_style_text_font(zmk_widget_layer_status_obj(&layer_status_widget),
                               lv_theme_get_font_small(screen), LV_PART_MAIN);
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget), LV_ALIGN_BOTTOM_LEFT, 0, 0);
#endif

    central_label = lv_label_create(screen);
    lv_obj_align(central_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    peripheral_label = lv_label_create(screen);
    lv_obj_align(peripheral_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    widget_sofle_battery_init();

    return screen;
}
