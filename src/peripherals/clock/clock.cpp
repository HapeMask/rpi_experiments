#include "peripherals/clock/clock.hpp"

Clock::Clock(): Peripheral(CLK_BASE_OFS, CLK_LEN) {
    _clk_pwm_ctl_reg = (volatile ClockControl*)reg_addr(CLK_PWM_CTL_OFS);
    _clk_pwm_div_reg = (volatile ClockDivider*)reg_addr(CLK_PWM_DIV_OFS);

    _clk_smi_ctl_reg = (volatile ClockControl*)reg_addr(CLK_SMI_CTL_OFS);
    _clk_smi_div_reg = (volatile ClockDivider*)reg_addr(CLK_SMI_DIV_OFS);
}

Clock::~Clock() {
    if (_virt_regs_ptr) {
        write_reg_with_sleep(_clk_pwm_ctl_reg, ClockControl{{.kill=1}}.bits);
        write_reg_with_sleep(_clk_smi_ctl_reg, ClockControl{{.kill=1}}.bits);
    }
}
