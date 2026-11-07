/** @file rc_safety.c */
#include "rc_safety.h"

#include <string.h>

static bool channel_in_range(uint16_t value, const RcSafetyConfig *cfg)
{
    return value >= cfg->valid_min && value <= cfg->valid_max;
}

static bool axes_neutral(const RcSafety *state)
{
    return state->sticks_neutral;
}

static bool channel_neutral(uint16_t value, const RcSafetyConfig *cfg)
{
    const int32_t delta = (int32_t)value - (int32_t)cfg->center;
    const int32_t magnitude = (delta < 0) ? -delta : delta;
    return magnitude <= (int32_t)cfg->deadband;
}

static void disarm(RcSafety *state, bool require_new_low)
{
    state->armed = false;
    state->arming = false;
    if (require_new_low) {
        state->arm_low_seen = false;
    }
}

void rc_safety_init(RcSafety *state, const RcSafetyConfig *config)
{
    if (state == 0) {
        return;
    }
    memset(state, 0, sizeof(*state));
    if (config != 0) {
        state->cfg = *config;
    }
}

bool rc_safety_accept_frame(RcSafety *state, const IBusChannels *channels,
                            uint32_t now_ms)
{
    if (state == 0 || channels == 0) {
        return false;
    }
    if (state->valid_frame_seen
        && (now_ms - state->last_frame_ms) > state->cfg.frame_timeout_ms) {
        state->valid_frame_seen = false;
        disarm(state, true);
    }

    const uint16_t indexes[] = {
        RC_CHANNEL_X_INDEX, RC_CHANNEL_Y_INDEX, RC_CHANNEL_YAW_INDEX,
        RC_CHANNEL_ARM_INDEX, RC_CHANNEL_MODE_INDEX
    };
    for (uint8_t i = 0u; i < (uint8_t)(sizeof(indexes) / sizeof(indexes[0])); i++) {
        if (!channel_in_range(channels->channels[indexes[i]], &state->cfg)) {
            state->valid_frame_seen = false;
            disarm(state, true);
            return false;
        }
    }

    const uint16_t arm = channels->channels[RC_CHANNEL_ARM_INDEX];
    const uint16_t mode = channels->channels[RC_CHANNEL_MODE_INDEX];
    const bool arm_low = arm <= state->cfg.switch_low_max;
    const bool arm_high = arm >= state->cfg.switch_high_min;
    const bool mode_low = mode <= state->cfg.switch_low_max;
    const bool mode_high = mode >= state->cfg.switch_high_min;
    if ((!arm_low && !arm_high) || (!mode_low && !mode_high)) {
        state->valid_frame_seen = false;
        disarm(state, true);
        return false;
    }

    state->valid_frame_seen = true;
    state->last_frame_ms = now_ms;
    state->axis_x = ibus_channel_unit(
        channels->channels[RC_CHANNEL_X_INDEX], state->cfg.center,
        state->cfg.span, state->cfg.deadband);
    state->axis_y = ibus_channel_unit(
        channels->channels[RC_CHANNEL_Y_INDEX], state->cfg.center,
        state->cfg.span, state->cfg.deadband);
    state->axis_yaw = ibus_channel_unit(
        channels->channels[RC_CHANNEL_YAW_INDEX], state->cfg.center,
        state->cfg.span, state->cfg.deadband);
    state->sticks_neutral = channel_neutral(
        channels->channels[RC_CHANNEL_X_INDEX], &state->cfg)
        && channel_neutral(channels->channels[RC_CHANNEL_Y_INDEX], &state->cfg)
        && channel_neutral(channels->channels[RC_CHANNEL_YAW_INDEX], &state->cfg);
    state->auto_selected = mode_high;

    if (arm_low) {
        state->arm_low_seen = true;
        disarm(state, false);
        return true;
    }
    if (!state->arm_low_seen) {
        disarm(state, false);
        return true;
    }
    if (state->armed) {
        return true;
    }
    if (!axes_neutral(state)) {
        state->arming = false;
        return true;
    }
    if (!state->arming) {
        state->arming = true;
        state->neutral_since_ms = now_ms;
        return true;
    }
    if ((now_ms - state->neutral_since_ms) >= state->cfg.neutral_hold_ms) {
        state->armed = true;
        state->arming = false;
    }
    return true;
}

RcSafetyOutput rc_safety_poll(RcSafety *state, uint32_t now_ms)
{
    RcSafetyOutput output;
    memset(&output, 0, sizeof(output));
    output.mode = RC_CONTROL_DISABLED;
    if (state == 0) {
        return output;
    }

    const bool fresh = state->valid_frame_seen
        && (now_ms - state->last_frame_ms) <= state->cfg.frame_timeout_ms;
    if (!fresh) {
        disarm(state, true);
    }
    output.frame_fresh = fresh;
    output.armed = fresh && state->armed;
    output.axis_x = state->axis_x;
    output.axis_y = state->axis_y;
    output.axis_yaw = state->axis_yaw;
    if (!output.armed) {
        return output;
    }

    const bool stick_active = !axes_neutral(state);
    output.manual_override = state->auto_selected && stick_active;
    output.mode = (state->auto_selected && !stick_active)
        ? RC_CONTROL_AUTO : RC_CONTROL_MANUAL;
    return output;
}
