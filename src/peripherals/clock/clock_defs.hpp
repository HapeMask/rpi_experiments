#pragma once

#include <cstdint>
#include <thread>

static constexpr uint32_t CLK_BASE_OFS   = 0x00101000;

// Not really, but long enough to get the registers we care about.
static constexpr uint32_t CLK_LEN        = 0xB8;
                       
static constexpr uint32_t CLK_PWM_CTL_OFS  = 0xa0;
static constexpr uint32_t CLK_PWM_DIV_OFS  = 0xa4;
static constexpr uint32_t CLK_SMI_CTL_OFS  = 0xb0;
static constexpr uint32_t CLK_SMI_DIV_OFS  = 0xb4;
                       
static constexpr uint32_t CLK_SRC_GND    = 0;
static constexpr uint32_t CLK_SRC_OSC    = 1;
static constexpr uint32_t CLK_SRC_TDBG0  = 2;
static constexpr uint32_t CLK_SRC_TDBG1  = 3;
static constexpr uint32_t CLK_SRC_PLLA   = 4;
static constexpr uint32_t CLK_SRC_PLLC   = 5;
static constexpr uint32_t CLK_SRC_PLLD   = 6;
static constexpr uint32_t CLK_SRC_HDMI   = 7;
                       
static constexpr uint32_t BCM_PASSWD     = 0x5A;

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

// Helpers to write to a register and wait to avoid overwhelming the bus.
inline void write_reg_with_sleep(volatile void* reg, uint32_t val, int sleep_ms=10) {
    *(volatile uint32_t*)reg = val;
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
}
