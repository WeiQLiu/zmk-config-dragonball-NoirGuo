/*
 * Copyright (c) 2026 wqliu
 *
 * SPDX-License-Identifier: MIT
 *
 * Movement-gated temporary mouse layer.
 *
 * This input processor is a drop-in replacement for
 * zmk,input-processor-temp-layer on pointer input listeners. The differences
 * are:
 *
 *   1. The layer is only activated once the pointer has moved a configurable
 *      distance within a short time window. A tiny bump, a trackball click or
 *      chassis vibration from a key bottom-out no longer switches layers.
 *
 *   2. While the layer is active, every movement event restarts the idle
 *      timeout, and pressing (or releasing) a non-transparent key of the
 *      target layer restarts it as well. Keys that are &trans do not extend
 *      the timeout, so normal typing still lets the layer fall away quickly.
 *
 * param1 = target layer index
 * param2 = idle timeout in milliseconds (0 selects the 400 ms default)
 */

#define DT_DRV_COMPAT zmk_input_processor_mouse_layer_gate

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/input_processor.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* The exact string ZMK stores in a keymap binding for &trans. ZMK uses
 * DEVICE_DT_NAME() for both sides, so this comparison stays correct even if
 * the transparent behavior gains a `label` property. */
#define MLG_TRANS_BEHAVIOR_NAME DEVICE_DT_NAME(DT_NODELABEL(trans))

/* Charybdis has 56 key positions; keep the guard below the 64-bit shift
 * boundary in case the layout grows. */
#define MLG_MAX_POSITION 63
#define MLG_DEFAULT_TIMEOUT_MS 400

struct mlg_config {
    uint16_t activation_threshold;
    uint16_t activation_window_ms;
};

struct mlg_data {
    struct k_mutex lock;
    struct k_work_delayable idle_work;
    uint8_t layer;
    uint16_t idle_timeout_ms;
    bool active;
    uint32_t accumulated;
    int64_t last_motion_ms;
};

static bool binding_is_transparent(uint8_t layer, uint32_t position) {
    const struct zmk_behavior_binding *binding =
        zmk_keymap_get_layer_binding_at_idx(layer, position);

    /* An unmapped position cannot be a real key press on this layer; treat
     * it like &trans and do not extend the timeout. */
    if (binding == NULL) {
        return true;
    }

    /* A real binding whose name is unavailable (local-id builds) is not
     * transparent, so let it extend the timeout. */
    if (binding->behavior_dev == NULL) {
        return false;
    }

    return strcmp(binding->behavior_dev, MLG_TRANS_BEHAVIOR_NAME) == 0;
}

static void arm_idle_timer(struct mlg_data *data) {
    k_work_reschedule(&data->idle_work, K_MSEC(data->idle_timeout_ms));
}

static void mlg_idle_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct mlg_data *data = CONTAINER_OF(dwork, struct mlg_data, idle_work);

    k_mutex_lock(&data->lock, K_FOREVER);

    if (!data->active) {
        k_mutex_unlock(&data->lock);
        return;
    }

    if (zmk_keymap_layer_active(data->layer)) {
        zmk_keymap_layer_deactivate(data->layer);
        LOG_DBG("mouse layer %d deactivated after timeout", data->layer);
    }

    /* The layer may also have been switched off by &to / &mo in the meantime;
     * in both cases it is no longer ours. */
    data->active = false;
    data->accumulated = 0;

    k_mutex_unlock(&data->lock);
}

static int mlg_handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
                            uint32_t param2, struct zmk_input_processor_state *state) {
    const struct mlg_config *cfg = dev->config;
    struct mlg_data *data = dev->data;

    ARG_UNUSED(state);

    if (event->type != INPUT_EV_REL ||
        (event->code != INPUT_REL_X && event->code != INPUT_REL_Y)) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    k_mutex_lock(&data->lock, K_FOREVER);

    data->layer = (uint8_t)param1;
    data->idle_timeout_ms = (param2 > 0) ? (uint16_t)param2 : MLG_DEFAULT_TIMEOUT_MS;

    int64_t now = k_uptime_get();

    if (data->active) {
        if (zmk_keymap_layer_active(data->layer)) {
            /* Still mousing: push the timeout out. */
            arm_idle_timer(data);
            k_mutex_unlock(&data->lock);
            return ZMK_INPUT_PROC_CONTINUE;
        }

        /* Layer was deactivated externally (&to 0, &mo release, ...). */
        data->active = false;
        data->accumulated = 0;
    }

    if (zmk_keymap_layer_active(data->layer)) {
        /* Layer is on because the user asked for it, not because of us. Do
         * not attach our timeout to it. */
        data->accumulated = 0;
        k_mutex_unlock(&data->lock);
        return ZMK_INPUT_PROC_CONTINUE;
    }

    if ((now - data->last_motion_ms) > cfg->activation_window_ms) {
        data->accumulated = 0;
    }
    data->last_motion_ms = now;

    int32_t value = event->value;
    if (value < 0) {
        value = -value;
    }
    data->accumulated += (uint32_t)value;

    if (data->accumulated < cfg->activation_threshold) {
        k_mutex_unlock(&data->lock);
        return ZMK_INPUT_PROC_CONTINUE;
    }

    data->accumulated = 0;

    /* set_layer_state() returns the event-manager result, not "did the layer
     * turn on", so check the layer state instead of the return value. */
    zmk_keymap_layer_activate(data->layer);
    if (zmk_keymap_layer_active(data->layer)) {
        data->active = true;
        arm_idle_timer(data);
        LOG_DBG("mouse layer %d activated after %u counts", data->layer,
                (unsigned int)cfg->activation_threshold);
    }

    k_mutex_unlock(&data->lock);
    return ZMK_INPUT_PROC_CONTINUE;
}

static int mlg_position_state_changed(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL || ev->position > MLG_MAX_POSITION) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct device *dev = DEVICE_DT_INST_GET(0);
    struct mlg_data *data = dev->data;

    k_mutex_lock(&data->lock, K_FOREVER);

    if (!data->active) {
        k_mutex_unlock(&data->lock);
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (!zmk_keymap_layer_active(data->layer)) {
        data->active = false;
        data->accumulated = 0;
        k_mutex_unlock(&data->lock);
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* Keys resolved on a layer above the mouse layer (a momentary &mo, a
     * layer-tap that is currently held, ...) are not ours. */
    if (zmk_keymap_layer_index_to_id(zmk_keymap_highest_layer_active()) != data->layer) {
        k_mutex_unlock(&data->lock);
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* &trans keys fall through to the layer below, so they are normal typing
     * and must not keep the mouse layer alive. Everything else on the mouse
     * layer (mouse buttons, &to 0, &lt, ...) restarts the timeout. */
    if (binding_is_transparent(data->layer, ev->position)) {
        k_mutex_unlock(&data->lock);
        return ZMK_EV_EVENT_BUBBLE;
    }

    arm_idle_timer(data);
    LOG_DBG("position %u on mouse layer restarts timeout", ev->position);

    k_mutex_unlock(&data->lock);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(mouse_layer_gate, mlg_position_state_changed);
ZMK_SUBSCRIPTION(mouse_layer_gate, zmk_position_state_changed);

static int mlg_init(const struct device *dev) {
    struct mlg_data *data = dev->data;

    k_mutex_init(&data->lock);
    k_work_init_delayable(&data->idle_work, mlg_idle_work_cb);
    data->idle_timeout_ms = MLG_DEFAULT_TIMEOUT_MS;

    return 0;
}

static const struct zmk_input_processor_driver_api mlg_api = {
    .handle_event = mlg_handle_event,
};

#define MLG_INST(n)                                                                                \
    static const struct mlg_config mlg_config_##n = {                                              \
        .activation_threshold = DT_INST_PROP_OR(n, activation_threshold, 30),                      \
        .activation_window_ms = DT_INST_PROP_OR(n, activation_window_ms, 300),                     \
    };                                                                                             \
    static struct mlg_data mlg_data_##n;                                                           \
    DEVICE_DT_INST_DEFINE(n, mlg_init, NULL, &mlg_data_##n, &mlg_config_##n, POST_KERNEL,          \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &mlg_api);

DT_INST_FOREACH_STATUS_OKAY(MLG_INST)
