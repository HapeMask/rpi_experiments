#pragma once

static constexpr int ARM_TIMER_BASE_OFS  = 0x0000B000;
static constexpr int ARM_TIMER_LEN  = 0x428;

static constexpr int ARM_TIMER_LOAD_OFS       = 0x400;
static constexpr int ARM_TIMER_VAL_OFS        = 0x404;
static constexpr int ARM_TIMER_CTL_OFS        = 0x408;
static constexpr int ARM_TIMER_RELOAD_OFS     = 0x418;
static constexpr int ARM_TIMER_PRE_DIV_OFS    = 0x41C;
static constexpr int ARM_TIMER_FREE_CTR_OFS   = 0x420;

static constexpr int ARM_TIMER_RESET_BITS  = 0x3E0020;

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
