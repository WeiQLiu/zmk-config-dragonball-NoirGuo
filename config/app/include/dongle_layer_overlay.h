/* Header for dongle layer overlay (currently minimal). */
#ifndef DONGLE_LAYER_OVERLAY_H
#define DONGLE_LAYER_OVERLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void zmk_dongle_display_draw_overlay(const char *text, uint8_t x, uint8_t y);

#ifdef __cplusplus
}
#endif

#endif /* DONGLE_LAYER_OVERLAY_H */
