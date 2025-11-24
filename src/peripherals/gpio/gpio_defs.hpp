#pragma once

#include <cstdint>

#define GPIO_BASE_OFS 0x00200000
#define GPIO_LEN 0xF4

#define GPIO_MODE_OFS 0x00
#define GPIO_OUT_SET_OFS 0x1C
#define GPIO_OUT_CLR_OFS 0x28
#define GPIO_LVL_OFS 0x34

enum class GPIOMode : uint32_t {
    IN =    0,
    OUT =   1,
    ALT_0 = 4,
    ALT_1 = 5,
    ALT_2 = 6,
    ALT_3 = 7,
    ALT_4 = 3,
    ALT_5 = 2
};

#define N_GPIO_MODE_REGS 6
#define N_GPIO_SET_CLR_REGS 2
#define N_GPIO_LVL_REGS 2
