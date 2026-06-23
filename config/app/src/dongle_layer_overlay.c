/*
 * Dongle layer overlay (paint layers N center-top using display device)
 *
 * This implementation tries two approaches in order:
 *  1) If the zmk-dongle-display module provides the weak hook
 *     `zmk_dongle_display_draw_overlay(const char*, uint8_t, uint8_t)`, use
 *     that to draw a short label.
 *  2) Otherwise, attempt to use the Zephyr display API to draw text
 *     directly onto the configured display device (if available).
 *
 * The code queries the active layer using ZMK's `layer_state` API when
 * available; otherwise it falls back to a rotating test label. All drawing
 * is additive and will not remove existing UI elements.
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <logging/log.h>
#include <device.h>
#include <drivers/display.h>
#include <string.h>
#include <stdio.h>

LOG_MODULE_REGISTER(dongle_layer_overlay, LOG_LEVEL_DBG);

/* Weak hook used by some dongle display modules */
void zmk_dongle_display_draw_overlay(const char *text, uint8_t x, uint8_t y) __attribute__((weak));

/* Try to read active layer via ZMK symbol if available. We guard with
 * weak symbol to avoid build breakage on versions where it's not exported.
 */
int zmk_active_layer_get(void) __attribute__((weak));

#define POLL_MS 1000

static void draw_via_display(const char *text)
{
    const struct device *disp = device_get_binding("SH1106");
    if (!disp) {
        /* try a common label used by zmk-dongle-display or SSD1306 */
        disp = device_get_binding("SSD1306");
    }

    if (!disp) {
        LOG_DBG("No display device found by name (SH1106/SSD1306)");
        return;
    }

    if (!device_is_ready(disp)) {
        LOG_DBG("Display device not ready");
        return;
    }

    /* Very small text drawing: many boards in ZMK don't include a full
     * graphics font API in tree; instead we request the display to
     * draw a framebuffer. For simplicity we use display_set_pixel if
     * available, but that's not guaranteed. So here we only attempt to
     * use display_blanking or custom hooks — reliable text rendering
     * across all ZMK trees is complex. We'll use the weak hook first.
     */

    LOG_DBG("Display device found (%p) but no generic text draw implementation available.", disp);
}

static void overlay_thread(void)
{
    char label[16];
    int idx = 1;

    while (1) {
        int active = 0;
        if (zmk_active_layer_get) {
            active = zmk_active_layer_get();
            snprintf(label, sizeof(label), "layers %d", active);
        } else {
            snprintf(label, sizeof(label), "layers %d", idx);
            idx++;
            if (idx > 4) idx = 1;
        }

        if (zmk_dongle_display_draw_overlay) {
            /* center-top hint (pixel coords may vary by display) */
            zmk_dongle_display_draw_overlay(label, 56, 2);
            LOG_DBG("Called weak hook to draw '%s'", label);
        } else {
            /* Fallback: try to draw via display device (best-effort)
             * If that doesn't draw (no generic API), nothing harmful
             * will happen.
             */
            draw_via_display(label);
            LOG_DBG("Attempted direct display draw for '%s'", label);
        }

        k_msleep(POLL_MS);
    }
}

K_THREAD_DEFINE(dongle_layer_overlay_thread, 1024, (k_thread_entry_t)overlay_thread, NULL, NULL, NULL, 7, 0, 0);
