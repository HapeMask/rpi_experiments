#pragma once

#include <cstdint>

static constexpr uint32_t GPIO_BASE_OFS     = 0x00200000;
static constexpr uint32_t GPIO_LEN          = 0xF4;

static constexpr uint32_t GPIO_MODE_OFS     = 0x00;
static constexpr uint32_t GPIO_OUT_SET_OFS  = 0x1C;
static constexpr uint32_t GPIO_OUT_CLR_OFS  = 0x28;
static constexpr uint32_t GPIO_LVL_OFS      = 0x34;

static constexpr uint32_t N_GPIO_MODE_REGS      = 6;
static constexpr uint32_t N_GPIO_SET_CLR_REGS   = 2;
static constexpr uint32_t N_GPIO_LVL_REGS       = 2;

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
