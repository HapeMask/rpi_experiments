#pragma once

#include <cstdint>
#include <thread>

enum class ClockID : uint32_t;

static constexpr uint32_t CLK_BASE_OFS   = 0x00101000;

// Not really, but long enough to get the registers we care about.
// See: https://elinux.org/BCM2835_registers#CM
// for more clocks / registers.
static constexpr uint32_t CLK_LEN        = 0xB8;

inline constexpr uint32_t CLK_CTL_OFS(ClockID id) { return (uint32_t)id * 8 + 0; }
inline constexpr uint32_t CLK_DIV_OFS(ClockID id) { return (uint32_t)id * 8 + 4; }

enum class ClockID : uint32_t {
    // Values derived from the offsets in https://elinux.org/BCM2835_registers#CM
    GP0 = 14,
    GP1 = 15,
    GP2 = 16,
    PWM = 20,
    SMI = 22
};
// TODO: Is there a better way to iterate over the enum values? Seems sad...
static constexpr ClockID ALL_CLOCKS[] = {
    ClockID::GP0,
    ClockID::GP1,
    ClockID::GP2,
    ClockID::PWM,
    ClockID::SMI
};
static constexpr int N_CLOCKS = sizeof(ALL_CLOCKS) / sizeof(ClockID);

enum class ClockSource : uint32_t {
    GND    = 0,
    OSC    = 1,
    TDBG0  = 2,
    TDBG1  = 3,
    PLLA   = 4,
    PLLC   = 5,
    PLLD   = 6,
    HDMI   = 7
};

static constexpr uint32_t BCM_PASSWD     = 0x5A;

union ClockControl {
    struct {
        ClockSource src         : 4 = ClockSource::GND;
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
