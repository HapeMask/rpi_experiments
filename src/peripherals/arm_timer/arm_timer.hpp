#pragma once

#include "peripherals/clock/clk_defs.hpp"
#include "utils/reg_mem_utils.hpp"

class ARMTimer {
    public:
        ARMTimer();
        virtual ~ARMTimer();

        void start();
        void stop();

        inline uint32_t read() const {
            return *_free_ctr_reg;
        }

    protected:
        void* _virt_timer_regs = nullptr;
        volatile TimerControl* _ctl_reg;
        volatile uint32_t* _free_ctr_reg;

        AddressSpaceInfo _asi;
};
