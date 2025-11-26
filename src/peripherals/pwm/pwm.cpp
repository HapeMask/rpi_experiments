#include <chrono>
#include <sstream>
#include <thread>
#include <unistd.h>

#include "peripherals/clock/clock_defs.hpp"
#include "peripherals/peripheral.hpp"
#include "peripherals/pwm/pwm.hpp"
#include "peripherals/pwm/pwm_defs.hpp"

PWM::PWM(float duty_cycle, float freq, bool use_fifo) :
    Peripheral(PWM_BASE_OFS, PWM_LEN),
    _use_fifo(use_fifo)
{
    _ctl_reg = (volatile PWMControl*) reg_addr(PWM_CTL_OFS);
    _sta_reg = (volatile PWMStatus*) reg_addr(PWM_STA_OFS);
    _dmac_reg = (volatile PWMDMAControl*) reg_addr(PWM_DMAC_OFS);
    _dat_reg = reg_addr(PWM0_DAT_OFS);
    _rng_reg = reg_addr(PWM0_RNG_OFS);
    _fif_reg = reg_addr(PWM0_FIF_OFS);

    setup_clock(duty_cycle, freq);
}

PWM::~PWM() {
    if (_virt_regs_ptr) {
        write_reg_with_sleep(_ctl_reg, PWMControl{{.enable_1=0}}.bits);
    }
}

void PWM::setup_clock(float duty_cycle, float freq) {
    _duty_cycle = duty_cycle;
    _freq = freq;

    stop();

    if (!_use_m_s) {
        // In non-M/S mode, the on and off pulses are multiples of a clock
        // cycle, TODO FIX THIS
        throw std::runtime_error("Not implemented.");
    }

    // Use the highest frequency (can't use full freq. since that results in a
    // divider of 1) for the best precision when using M/S mode. Otherwise, try
    // to get the PWM range to be as close as possible to the requested
    // frequency.
    const float tgt_freq = _use_m_s ? (CLOCK_HZ[ClockSource::PLLD] / 2) : freq;
    const float real_clk_freq = _clock.start_clock(ClockID::PWM, ClockSource::PLLD, tgt_freq);

    write_reg_with_sleep(_sta_reg, ~0);

    if (_use_m_s) {
        write_reg_with_sleep(_dat_reg, (uint32_t)(duty_cycle * real_clk_freq / freq));
        write_reg_with_sleep(_rng_reg, (uint32_t)(real_clk_freq / freq));
    } else {
        // TODO
        write_reg_with_sleep(_dat_reg, 1);
        write_reg_with_sleep(_rng_reg, 2);
    }
}

void PWM::start() {
    // Reset PWM
    write_reg_with_sleep(_ctl_reg, 0);
    write_reg_with_sleep(_sta_reg, ~0);

    // Start PWM
    write_reg_with_sleep(
        _ctl_reg,
        PWMControl{{.enable_1=1, .use_fifo_1=_use_fifo, .use_m_s_1=_use_m_s}}.bits
    );
}

void PWM::stop() {
    write_reg_with_sleep(_ctl_reg, PWMControl{{.enable_1=0, .enable_2=0}}.bits);
}

void PWM::enable_dma(uint32_t dreq_thresh, uint32_t panic_thresh) {
    write_reg_with_sleep(
        _dmac_reg,
        PWMDMAControl{{
            .dma_req_thresh=dreq_thresh,
            .dma_panic_thresh=panic_thresh,
            .enable=1
        }}.bits
    );
}

void PWM::disable_dma() {
    write_reg_with_sleep(_dmac_reg, PWMDMAControl{{.enable=0}}.bits);
}
