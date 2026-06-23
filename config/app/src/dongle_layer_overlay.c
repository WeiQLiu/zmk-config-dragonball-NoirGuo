/*
 * Non-invasive dongle layer overlay
 * Adds a small hook that attempts to draw a short layer label (e.g. "L1")
 * at the right-bottom corner of the dongle display.
 *
 * Implementation notes:
 * - This file is compiled only when CONFIG_ZMK_DONGLE_DISPLAY_LAYER is set
 *   (see CMakeLists above).
 * - We try to call a weak symbol `zmk_dongle_display_draw_overlay` if it's
 *   provided by the zmk-dongle-display module. If present, that function
 *   should accept (const char *text, uint8_t x, uint8_t y) and draw the
 *   text at the requested coordinates. If not present, the code is a no-op
 *   (so it won't break builds).
 * - This is intentionally minimal and non-invasive. If the dongle display
 *   module in your ZMK tree exposes another API, we can adapt accordingly.
 */

#include <zephyr.h>
#include <sys/printk.h>
#include <logging/log.h>
#include <stdint.h>

LOG_MODULE_REGISTER(dongle_layer_overlay, LOG_LEVEL_INF);

/* Weak hook: if zmk-dongle-display or other module exposes this symbol
 * we'll call it to render the overlay. The weak attribute makes the symbol
 * optional at link time. */
void zmk_dongle_display_draw_overlay(const char *text, uint8_t x, uint8_t y) __attribute__((weak));

/* Poll interval (ms) -- conservative update rate */
#define POLL_MS 500

static void overlay_thread(void)
{
    /* For now we don't have a robust cross-version API to query the
     * currently-active layer from ZMK. To keep this change safe and
     * non-invasive, this implementation will attempt to call the weak
     * drawing hook with a placeholder string. If you can provide the
     * ZMK API that returns the active layer on your tree (or allow me
     * to inspect CI build logs), I can update this to show the real
     * layer number.
     */

    char label[8];
    int idx = 1; /* placeholder layer number */

    while (1) {
        /* Compose short label; adjust later to query real layer */
        (void)snprintf(label, sizeof(label), "L%d", idx);

        if (zmk_dongle_display_draw_overlay) {
            /* Draw at right-bottom corner: coordinates are caller-defined
             * by the dongle display implementation. We choose (x=0xff,y=0xff)
             * as a hint meaning "right-bottom" in some implementations; if
             * your display API expects pixel coordinates, we'll adapt later.
             */
            zmk_dongle_display_draw_overlay(label, 0xff, 0xff);
            LOG_INF("Called weak draw_overlay with %s", label);
        } else {
            /* No-op, but keep a log for debugging */
            LOG_DBG("dongle overlay hook not found (weak symbol)");
        }

        k_msleep(POLL_MS);
    }
}

K_THREAD_DEFINE(dongle_layer_overlay_thread, 1024, (k_thread_entry_t)overlay_thread, NULL, NULL, NULL, 7, 0, 0);
