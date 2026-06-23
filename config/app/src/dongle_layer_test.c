/*
 * Minimal dongle display test helper
 *
 * This file attempts several non-destructive ways to draw a very visible
 * test mark on the dongle OLED so we can determine whether overlay code is
 * actually running and able to write pixels.
 *
 * Strategy (non-destructive):
 *  - Log thread start.
 *  - If a weak-drawn hook is exported by any display module (convention:
 *    zmk_dongle_display_draw_overlay), call it with "TEST" at center-top.
 *  - Otherwise try to find a display device by a few common binding labels
 *    ("oled", "SH1106", "SSD1306") and log that we found it. If found,
 *    call display_blanking_off() to make sure the display is enabled.
 *  - This file intentionally does NOT attempt risky framebuffer writes
 *    across all drivers; it is primarily a litmus test that the thread
 *    runs and the display device is reachable. If this test shows the
 *    device is reachable, I will follow up with a direct pixel/bitmap
 *    rendering implementation for your specific driver.
 */

#include <zephyr.h>
#include <device.h>
#include <drivers/display.h>
#include <logging/log.h>
#include <stdio.h>

LOG_MODULE_REGISTER(dongle_layer_test, LOG_LEVEL_DBG);

/* Weak hook which some dongle display modules may expose. */
void zmk_dongle_display_draw_overlay(const char *text, uint8_t x, uint8_t y) __attribute__((weak));

#define POLL_MS 2000

static void test_thread(void)
{
    LOG_INF("dongle_layer_test thread started");

    while (1) {
        /* 1) Try weak hook if present */
        if (zmk_dongle_display_draw_overlay) {
            LOG_INF("dongle_layer_test: calling weak hook to draw TEST");
            zmk_dongle_display_draw_overlay("TEST", 56, 2);
            /* Let caller see it for a while */
            k_msleep(1500);
            /* call again to ensure visible */
            zmk_dongle_display_draw_overlay("    ", 56, 2);
            LOG_INF("dongle_layer_test: draw via weak hook done");
            k_msleep(POLL_MS);
            continue;
        }

        /* 2) Try to find a display device by common names */
        const struct device *disp = NULL;
        const char *names[] = {"oled", "SH1106", "SSD1306", "SH1106_0", "SSD1306_0", NULL};
        for (int i = 0; names[i] != NULL; i++) {
            disp = device_get_binding(names[i]);
            if (disp) {
                LOG_INF("dongle_layer_test: found display device by name '%s' (%p)", names[i], disp);
                break;
            }
        }

        /* 3) If not found, try DT_CHOSEN zephyr_display label (best-effort) */
#ifdef DT_CHOSEN
#ifdef DT_CHOSEN_ZEPHYR_DISPLAY_LABEL
        if (!disp) {
            disp = device_get_binding(DT_CHOSEN_ZEPHYR_DISPLAY_LABEL);
            if (disp) {
                LOG_INF("dongle_layer_test: found display via DT_CHOSEN label '%s'", DT_CHOSEN_ZEPHYR_DISPLAY_LABEL);
            }
        }
#endif
#endif

        if (disp) {
            if (device_is_ready(disp)) {
                LOG_INF("dongle_layer_test: display device ready: %p", disp);
                /* Try to un-blank the display so we can see any changes */
                if (display_blanking_off(disp) == 0) {
                    LOG_INF("dongle_layer_test: display_blanking_off() succeeded");
                } else {
                    LOG_WRN("dongle_layer_test: display_blanking_off() failed or unsupported");
                }

                /* Leave a short visible time for humans to see */
                k_msleep(1500);
            } else {
                LOG_WRN("dongle_layer_test: display device not ready");
            }
        } else {
            LOG_WRN("dongle_layer_test: no display device found by common names or DT_CHOSEN");
        }

        k_msleep(POLL_MS);
    }
}

K_THREAD_DEFINE(dongle_layer_test_thread, 1024, (k_thread_entry_t)test_thread, NULL, NULL, NULL, 6, 0, 0);
