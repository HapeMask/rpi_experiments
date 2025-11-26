#pragma once

#include <unordered_map>

#include "peripherals/peripheral.hpp"
#include "peripherals/clock/clock_defs.hpp"

class Clock : public Peripheral {
    public:
        Clock();
        virtual ~Clock();

        uint32_t start_clock(ClockID id, ClockSource src, float freq);

        volatile ClockControl* get_ctl_reg(ClockID id);
        volatile ClockDivider* get_div_reg(ClockID id);

    protected:
        std::unordered_map<ClockID, volatile ClockControl*> _clk_ctl_regs;
        std::unordered_map<ClockID, volatile ClockDivider*> _clk_div_regs;
};
