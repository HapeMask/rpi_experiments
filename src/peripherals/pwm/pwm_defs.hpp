#pragma once

#define PWM_BASE_OFS 0x0020C000
#define PWM_LEN 0x28

#define PWM_CTL_OFS     0x00
#define PWM_STA_OFS     0x04
#define PWM_DMAC_OFS    0x08
#define PWM0_RNG_OFS    0x10
#define PWM0_DAT_OFS    0x14
#define PWM0_FIF_OFS    0x18

union PWMControl {
    struct {
        uint32_t enable_1           : 1 = 0;
        uint32_t serialize_mode_1   : 1 = 0;
        uint32_t repeat_last_1      : 1 = 0;
        uint32_t silence_bit_1      : 1 = 0;
        uint32_t polarity_1         : 1 = 0;
        uint32_t use_fifo_1         : 1 = 0;
        uint32_t clear_fifo_1       : 1 = 0;
        uint32_t use_m_s_1          : 1 = 0;
        uint32_t enable_2           : 1 = 0;
        uint32_t serialize_mode_2   : 1 = 0;
        uint32_t repeat_last_2      : 1 = 0;
        uint32_t silence_bit_2      : 1 = 0;
        uint32_t polarity_2         : 1 = 0;
        uint32_t use_fifo_2         : 1 = 0;
        uint32_t unused_2           : 1 = 0;
        uint32_t use_m_s_2          : 1 = 0;
        uint32_t unused             : 16 = 0;
    } flags;
    uint32_t bits;
};

union PWMStatus {
    struct {
        uint32_t full           : 1 = 0;
        uint32_t empty          : 1 = 0;
        uint32_t write_error    : 1 = 0;
        uint32_t read_error     : 1 = 0;
        uint32_t gap_0          : 1 = 0;
        uint32_t gap_1          : 1 = 0;
        uint32_t gap_2          : 1 = 0;
        uint32_t gap_3          : 1 = 0;
        uint32_t bus_error      : 1 = 0;
        uint32_t state_0        : 1 = 0;
        uint32_t state_1        : 1 = 0;
        uint32_t state_2        : 1 = 0;
        uint32_t state_3        : 1 = 0;
        uint32_t unused         : 19 = 0;
    } flags;
    uint32_t bits;
};

union PWMDMAControl {
    struct {
        uint32_t dma_req_thresh     : 8 = 0;
        uint32_t dma_panic_thresh   : 8 = 0;
        uint32_t unused             : 15 = 0;
        uint32_t enable             : 1 = 0;
    } flags;
    uint32_t bits;
};
