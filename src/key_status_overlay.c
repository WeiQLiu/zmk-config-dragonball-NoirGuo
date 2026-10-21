/*
 * Last-pressed-key overlay for the Charybdis dongle OLED.
 *
 * The display itself is drawn by englmaxi/zmk-dongle-display.  That module
 * builds its status screen at boot, so instead of forking it this file adds a
 * small transparent LVGL box on top of the active screen, in the free area
 * between the top status row and the modifier row:
 *
 *     x = 31, y = 19, 46 x 26 pixels
 *
 * It is deliberately clear of:
 *   - the USB/BT status row (ends at y = 18),
 *   - the battery labels (top right),
 *   - the HID indicators / NLCK (bottom left),
 *   - the Bongo Cat (starts at x = 78),
 *   - the modifier icons (start at y = 47).
 *
 * The label only updates on key presses sent to the host, ignores pure
 * modifier presses (the modifier widget already shows those), and wraps long
 * names such as PageUp over two lines.  Short names are centred vertically.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>

#include <dt-bindings/zmk/hid_usage.h>

#include "keycode_name.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KEY_BOX_X 31
#define KEY_BOX_Y 19
#define KEY_BOX_W 46
#define KEY_BOX_H 26

struct key_status_state {
    uint32_t keycode;
    uint16_t usage_page;
    bool valid;
};

static lv_obj_t *key_box;
static lv_obj_t *key_label;

static bool is_modifier(uint32_t keycode) {
    return keycode >= HID_USAGE_KEY_KEYBOARD_LEFTCONTROL &&
           keycode <= HID_USAGE_KEY_KEYBOARD_RIGHT_GUI;
}

static void ensure_widget(void) {
    if (key_label != NULL || !zmk_display_is_initialized()) {
        return;
    }

    lv_obj_t *screen = lv_scr_act();
    if (screen == NULL) {
        return;
    }

    key_box = lv_obj_create(screen);
    lv_obj_remove_style_all(key_box);
    lv_obj_set_size(key_box, KEY_BOX_W, KEY_BOX_H);
    lv_obj_set_pos(key_box, KEY_BOX_X, KEY_BOX_Y);
    lv_obj_clear_flag(key_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(key_box, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_bg_opa(key_box, LV_OPA_TRANSP, 0);

    /* The display module creates its widgets during boot, so a box created
     * on the first key press is already on top.  Keep it there explicitly in
     * case another widget calls lv_obj_move_foreground() while updating. */
    lv_obj_move_foreground(key_box);

    key_label = lv_label_create(key_box);
    lv_obj_set_width(key_label, KEY_BOX_W);
    lv_label_set_long_mode(key_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(key_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_font(key_label, &lv_font_unscii_8, 0);
    /* The mono theme is light (black text on white) and the SH1106 panel is
     * inverted in hardware, so hard-coding white here makes the text
     * invisible.  Copy the screen's current text colour instead. */
    lv_obj_set_style_text_color(key_label, lv_obj_get_style_text_color(screen, 0), 0);
    lv_obj_set_style_text_line_space(key_label, 0, 0);
    lv_obj_set_style_bg_opa(key_label, LV_OPA_TRANSP, 0);
}

static void key_status_update_cb(struct key_status_state state) {
    ensure_widget();

    if (key_label == NULL) {
        return;
    }

    if (!state.valid) {
        lv_label_set_text(key_label, "-");
        lv_obj_center(key_label);
        return;
    }

    lv_label_set_text(key_label, keycode_to_string(state.keycode, state.usage_page));

    /* Re-centre after the text height changed (one line vs. two lines). */
    lv_obj_center(key_label);
    lv_obj_move_foreground(key_box);
}

static struct key_status_state key_status_get_state(const zmk_event_t *eh) {
    static struct key_status_state last;

    const struct zmk_keycode_state_changed *ev =
        eh != NULL ? as_zmk_keycode_state_changed(eh) : NULL;

    if (ev != NULL && ev->state && !is_modifier(ev->keycode)) {
        last.keycode = ev->keycode;
        last.usage_page = ev->usage_page;
        last.valid = true;
    }

    return last;
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_key_status, struct key_status_state, key_status_update_cb,
                            key_status_get_state)
ZMK_SUBSCRIPTION(widget_key_status, zmk_keycode_state_changed);

/*
 * Create the overlay once at boot as well, so the area shows a "-" before the
 * first key press.  The actual creation has to happen in the display work
 * queue; a small delayed work waits for zmk_display_init() to finish.
 */
static void key_status_create_work_cb(struct k_work *work) {
    if (key_label == NULL) {
        widget_key_status_init();
    }
}

K_WORK_DEFINE(key_status_create_work, key_status_create_work_cb);

static void key_status_delayed_create_cb(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(key_status_delayed_create, key_status_delayed_create_cb);

static void key_status_delayed_create_cb(struct k_work *work) {
    if (!zmk_display_is_initialized()) {
        k_work_reschedule(&key_status_delayed_create, K_MSEC(500));
        return;
    }

    k_work_submit_to_queue(zmk_display_work_q(), &key_status_create_work);
}

static int key_status_overlay_init(void) {
    k_work_schedule(&key_status_delayed_create, K_MSEC(800));
    return 0;
}

SYS_INIT(key_status_overlay_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
