#pragma once

#include "peripherals/peripheral.hpp"
#include "peripherals/clock/clock_defs.hpp"

class Clock : public Peripheral {
    public:
        Clock();
        virtual ~Clock();

    //TODO: Make an interface for this? Or public is fine...
    //protected:
        volatile ClockControl* _clk_pwm_ctl_reg = nullptr;
        volatile ClockDivider* _clk_pwm_div_reg = nullptr;

        volatile ClockControl* _clk_smi_ctl_reg = nullptr;
        volatile ClockDivider* _clk_smi_div_reg = nullptr;

        // TODO: Add the rest of the clocks here.
};
