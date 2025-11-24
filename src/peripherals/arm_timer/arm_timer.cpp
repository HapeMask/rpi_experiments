#include <chrono>
#include <thread>

#include "peripherals/arm_timer/arm_timer.hpp"

ARMTimer::ARMTimer() {
    _virt_timer_regs = map_phys_block((void*)(_asi.phys_mmio_base + ARM_TIMER_BASE_OFS), ARM_TIMER_LEN, _asi.page_size);
    _ctl_reg = (volatile TimerControl*) reg_addr(_virt_timer_regs, ARM_TIMER_CTL_OFS);
    _free_ctr_reg = reg_addr(_virt_timer_regs, ARM_TIMER_FREE_CTR_OFS);
}

ARMTimer::~ARMTimer() {
    if (_virt_timer_regs) {
        unmap_phys_block((void*)(_asi.phys_mmio_base + ARM_TIMER_BASE_OFS), ARM_TIMER_LEN, _asi.page_size);
    }
}

void ARMTimer::start() {
    _ctl_reg->bits = ARM_TIMER_RESET_BITS;
    std::this_thread::sleep_for(std::chrono::microseconds(100));
    _ctl_reg->bits = TimerControl{{.free_run_enable=1, .free_run_prescale=0}}.bits;
}

void ARMTimer::stop() {
    _ctl_reg->bits = ARM_TIMER_RESET_BITS;
}
