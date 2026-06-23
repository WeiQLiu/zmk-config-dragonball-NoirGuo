/*
 * Dongle layer overlay (improved diagnostics)
 *
 * This update does two things:
 *  - Adds clearer logging to help determine whether the weak hook is present.
 *  - Uses an explicit pixel coordinate hint (center-top) when calling the
 *    weak hook. If the weak hook is still absent, the code will log that and
 *    sleep. This avoids risky direct display driver calls which are fragile
 *    across ZMK/Zephyr versions.
 *
 * Once we confirm via logs whether the weak hook exists, we can either:
 *  - adapt to the dongle display API (call its pixel drawing functions) and
 *    render the real layer number, or
 *  - make the display module export a compatible hook.
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
 * where x,y are pixel coordinates. Some display modules may instead accept
 * special hint values (like 0xff) meaning "corner"; we'll use explicit
 * coords first.
 */
void zmk_dongle_display_draw_overlay(const char *text, uint8_t x, uint8_t y) __attribute__((weak));

#define POLL_MS 800

static void overlay_thread(void)
{
    char label[8];
    int idx = 1;
    const uint8_t draw_x = 56; /* center-ish for 129px width */
    const uint8_t draw_y = 2;  /* near top */

    LOG_INF("dongle_layer_overlay thread started");

    while (1) {
        /* For now rotate a visible label so we can test drawing
         * until we wire up the real layer query. */
        snprintf(label, sizeof(label), "L%d", idx);

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
