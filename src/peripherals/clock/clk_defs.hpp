#pragma once

#include <stdint.h>

#define BCM_PASSWD 0x5A

#define CLK_BASE_OFS 0x00101000
#define CLK_LEN 0xA8
#define ARM_TIMER_BASE_OFS 0x0000B000
#define ARM_TIMER_LEN 0x428

#define ARM_TIMER_LOAD_OFS      0x400
#define ARM_TIMER_VAL_OFS       0x404
#define ARM_TIMER_CTL_OFS       0x408
#define ARM_TIMER_RELOAD_OFS    0x418
#define ARM_TIMER_PRE_DIV_OFS   0x41C
#define ARM_TIMER_FREE_CTR_OFS  0x420

#define CLK_PWM_CTL_OFS 0xa0
#define CLK_PWM_DIV_OFS 0xa4

union ClockControl {
    struct {
        uint32_t src            : 4 = 0;
        uint32_t enable         : 1 = 0;
        uint32_t kill           : 1 = 0;
        uint32_t unused_0       : 1 = 0;
        uint32_t busy           : 1 = 0;
        uint32_t invert         : 1 = 0;
        uint32_t mash           : 2 = 0;
        uint32_t unused_1       : 13 = 0;
        const uint32_t passwd   : 8 = BCM_PASSWD;
    } flags;
    uint32_t bits;
};

union ClockDivider {
    struct {
        uint32_t fractional     : 12 = 0;
        uint32_t integer        : 12 = 0;
        const uint32_t passwd   : 8 = BCM_PASSWD;
    } flags;
    uint32_t bits;
};

#define CLK_SRC_GND   0
#define CLK_SRC_OSC   1
#define CLK_SRC_TDBG0 2
#define CLK_SRC_TDBG1 3
#define CLK_SRC_PLLA  4
#define CLK_SRC_PLLC  5
#define CLK_SRC_PLLD  6
#define CLK_SRC_HDMI  7

union TimerControl {
    struct {
        uint32_t unused_0           : 1 = 0;
        uint32_t use_23_bit_counter : 1 = 0;
        uint32_t pre_scale_16       : 1 = 0;
        uint32_t pre_scale_256      : 1 = 0;
        uint32_t unused_1           : 1 = 0;
        uint32_t interrupt_enabled  : 1 = 0;
        uint32_t unused_2           : 1 = 0;
        uint32_t enabled            : 1 = 0;
        uint32_t halt_on_debug_halt : 1 = 0;
        uint32_t free_run_enable    : 1 = 0;
        uint32_t unused_3           : 6 = 0;
        uint32_t free_run_prescale  : 8 = 0;
        uint32_t unused_4           : 8 = 0;
    } flags;
    uint32_t bits;
};

#define ARM_TIMER_RESET_BITS 0x3E0020
