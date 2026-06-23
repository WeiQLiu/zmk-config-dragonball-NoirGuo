/*
 * Dongle layer overlay (display "layers N" center-top)
 *
 * This update changes the diagnostic overlay to render a short label
 * "layers N" (N in 1..4 rotating) at the center-top of the screen using
 * the optional weak hook zmk_dongle_display_draw_overlay(...).
 *
 * This is still non-invasive: if the weak hook is not present, the code
 * will log a warning but won't break the build. Once we confirm the draw
 * hook exists (or adapt to the real display API), this file can be
 * updated to show the actual active layer.
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <logging/log.h>
#include <stdint.h>
#include <stdio.h>

LOG_MODULE_REGISTER(dongle_layer_overlay, LOG_LEVEL_INF);

/* Weak hook: if zmk-dongle-display or other module exposes this symbol
 * we'll call it to render the overlay. The weak attribute makes the symbol
 * optional at link time. The expected semantics (by convention here) are:
 *   void zmk_dongle_display_draw_overlay(const char *text, uint8_t x, uint8_t y)
 * where x,y are pixel coordinates.
 */
void zmk_dongle_display_draw_overlay(const char *text, uint8_t x, uint8_t y) __attribute__((weak));

#define POLL_MS 800

static void overlay_thread(void)
{
    char label[16];
    int idx = 1;
    const uint8_t draw_x = 56; /* center-ish for 129px width */
    const uint8_t draw_y = 2;  /* near top */

    LOG_INF("dongle_layer_overlay thread started");

    while (1) {
        /* Show "layers N" rotating for testing. We'll replace this with
         * the real layer number when we adapt to the ZMK layer API. */
        snprintf(label, sizeof(label), "layers %d", idx);

        if (zmk_dongle_display_draw_overlay) {
            LOG_INF("Calling draw_overlay: %s @%d,%d", label, draw_x, draw_y);
            zmk_dongle_display_draw_overlay(label, draw_x, draw_y);
        } else {
            LOG_WRN("dongle overlay hook not found (weak symbol). No drawing performed.");
        }

        /* rotate index for visible change during testing */
        idx++;
        if (idx > 4) idx = 1;

        k_msleep(POLL_MS);
    }
}

K_THREAD_DEFINE(dongle_layer_overlay_thread, 1024, (k_thread_entry_t)overlay_thread, NULL, NULL, NULL, 7, 0, 0);
