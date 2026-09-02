#pragma once

#include <pspctrl.h>
#include <cstdint>

#define MAPLE_FUNC_CONTROLLER 1
#define CONT_CAPABILITIES_DUAL_ANALOG 0

struct maple_device_t {};

struct cont_state_t {
    uint32_t a, b, c, d, x, y, z, start;
    uint32_t dpad_up, dpad_down, dpad_left, dpad_right;
    uint32_t ltrig, rtrig;
    int16_t joyx, joyy, joy2x, joy2y;
};

inline maple_device_t *maple_enum_type(int, int)
{
    static maple_device_t device;
    return &device;
}

inline void *maple_dev_status(maple_device_t *)
{
    static cont_state_t state{};
    SceCtrlData pad{};
    sceCtrlPeekBufferPositive(&pad, 1);
    state.a = !!(pad.Buttons & PSP_CTRL_CROSS);
    state.b = !!(pad.Buttons & PSP_CTRL_CIRCLE);
    state.x = !!(pad.Buttons & PSP_CTRL_SQUARE);
    state.y = !!(pad.Buttons & PSP_CTRL_TRIANGLE);
    state.c = state.d = state.z = 0;
    state.start = !!(pad.Buttons & PSP_CTRL_START);
    state.dpad_up = !!(pad.Buttons & PSP_CTRL_UP);
    state.dpad_down = !!(pad.Buttons & PSP_CTRL_DOWN);
    state.dpad_left = !!(pad.Buttons & PSP_CTRL_LEFT);
    state.dpad_right = !!(pad.Buttons & PSP_CTRL_RIGHT);
    state.ltrig = !!(pad.Buttons & PSP_CTRL_LTRIGGER);
    state.rtrig = !!(pad.Buttons & PSP_CTRL_RTRIGGER);
    state.joyx = int16_t(pad.Lx) - 128;
    state.joyy = int16_t(pad.Ly) - 128;
    state.joy2x = state.joy2y = 0;
    return &state;
}
