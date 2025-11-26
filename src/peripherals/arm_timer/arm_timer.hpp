#pragma once

#include "peripherals/arm_timer/arm_timer_defs.hpp"
#include "peripherals/peripheral.hpp"
#include "utils/reg_mem_utils.hpp"

class ARMTimer : public Peripheral {
    public:
        ARMTimer();
        virtual ~ARMTimer();

        void start();
        void stop();
        uint32_t read() const;

    protected:
        volatile TimerControl* _ctl_reg;
        volatile uint32_t* _free_ctr_reg;
};
