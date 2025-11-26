#include <chrono>
#include <thread>

#include "peripherals/arm_timer/arm_timer.hpp"
#include "peripherals/arm_timer/arm_timer_defs.hpp"

ARMTimer::ARMTimer() : Peripheral(ARM_TIMER_BASE_OFS, ARM_TIMER_LEN) {
    _ctl_reg = (volatile TimerControl*) reg_addr(ARM_TIMER_CTL_OFS);
    _free_ctr_reg = reg_addr(ARM_TIMER_FREE_CTR_OFS);
}

ARMTimer::~ARMTimer() { }

void ARMTimer::start() {
    _ctl_reg->bits = ARM_TIMER_RESET_BITS;
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    _ctl_reg->bits = TimerControl{{.free_run_enable=1, .free_run_prescale=0}}.bits;
}

void ARMTimer::stop() {
    _ctl_reg->bits = ARM_TIMER_RESET_BITS;
}

uint32_t ARMTimer::read() const {
    return *_free_ctr_reg;
}
