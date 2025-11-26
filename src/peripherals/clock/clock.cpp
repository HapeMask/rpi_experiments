#include <chrono>
#include <sstream>
#include <thread>

#include "peripherals/clock/clock.hpp"
#include "peripherals/clock/clock_defs.hpp"
#include "utils/rpi_zero_2.hpp"

Clock::Clock(): Peripheral(CLK_BASE_OFS, CLK_LEN) {
    for(int i=0; i<N_CLOCKS; ++i) {
        const auto clk_id = ALL_CLOCKS[i];
        _clk_ctl_regs[clk_id] = (volatile ClockControl*)reg_addr(CLK_CTL_OFS(clk_id));
        _clk_div_regs[clk_id] = (volatile ClockDivider*)reg_addr(CLK_DIV_OFS(clk_id));
    }
}

Clock::~Clock() {
    if (_virt_regs_ptr) {
        // Only kill clocks we have started, don't accidentally kill other
        // clocks.
        for(const auto& id : _started_clocks) {
            kill_clock(id);
        }
    }
}

void Clock::kill_clock(ClockID id) {
    auto ctl_reg = get_ctl_reg(id);

    // Kill the clock generator and wait for it to stop.
    write_reg_with_sleep(ctl_reg, ClockControl{{.kill=1}}.bits);
    while (ctl_reg->flags.busy) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

uint32_t Clock::start_clock(ClockID id, ClockSource src, float freq) {
    if (!CLOCK_HZ.contains(src)) {
        std::ostringstream ss;
        ss << "Unknown clock source: " << (uint32_t)src;
        throw std::runtime_error(ss.str());
    }

    auto ctl_reg = get_ctl_reg(id);
    auto div_reg = get_div_reg(id);
    auto clk_hz = CLOCK_HZ[src];

    kill_clock(id);

    // Compute the divider for the target frequency.
    const uint32_t clk_div_i = clk_hz / freq;

    if (clk_div_i < 2 || clk_div_i > 4095) {
        std::ostringstream ss;
        ss << "Desired frequency " << freq
           << "Hz and source clock frequency " << clk_hz
           << "Hz gives a bad clock divider: " << clk_div_i
           << ". Must be in [2, 4095].";
        throw std::runtime_error(ss.str());
    }

    write_reg_with_sleep(div_reg, ClockDivider{{.integer=clk_div_i}}.bits);

    // Start the clock.
    write_reg_with_sleep(ctl_reg, ClockControl{{.src=src, .enable=1}}.bits);

    // Wait for the clock to start up (become busy).
    while (!ctl_reg->flags.busy) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const uint32_t real_clk_freq = (clk_hz / clk_div_i);
    _started_clocks.push_back(id);
    return real_clk_freq;
}

volatile ClockControl* Clock::get_ctl_reg(ClockID id) {
    return _clk_ctl_regs[id];
}

volatile ClockDivider* Clock::get_div_reg(ClockID id) {
    return _clk_div_regs[id];
}
