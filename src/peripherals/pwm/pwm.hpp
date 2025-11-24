#pragma once

// Make/include the appropriate header for your device. TODO: Can these also be
// determined programmatically??
#include "utils/rpi_zero_2.hpp"

#include "peripherals/pwm/pwm_defs.hpp"
#include "peripherals/clock/clk_defs.hpp"
#include "utils/reg_mem_utils.hpp"

#define USE_PLLD_FOR_PWM_CLK

#ifdef USE_PLLD_FOR_PWM_CLK
#define PWM_CLK_HZ PLLD_CLK_HZ
#else
#define PWM_CLK_HZ OSC_CLK_HZ
#endif

#define MIN_CLK_HZ PWM_CLK_HZ / 4095
#define MAX_CLK_HZ PWM_CLK_HZ / 2

class PWM {
    public:
        PWM(float duty_cycle, float freq, bool use_fifo=false);
        virtual ~PWM();

        void setup_clock(float duty_cycle, float freq);
        void start();
        void stop();
        void enable_dma(uint32_t dreq_thresh=7, uint32_t panic_thresh=7);
        void disable_dma();

        uint32_t get_range() const { return *_rng_reg; }
        uint32_t get_data() const { return *_dat_reg; }

        void* reg_to_bus(uint32_t reg_ofs_bytes) const;

    protected:
        float _duty_cycle;
        float _freq;
        bool _use_m_s = true;
        bool _use_fifo;

        void* _virt_pwm_regs = nullptr;
        void* _virt_clk_regs = nullptr;
        volatile PWMControl* _ctl_reg = nullptr;
        volatile PWMStatus* _sta_reg = nullptr;
        volatile PWMDMAControl* _dmac_reg = nullptr;
        volatile uint32_t* _dat_reg = nullptr;
        volatile uint32_t* _rng_reg = nullptr;
        volatile uint32_t* _fif_reg = nullptr;
        volatile ClockControl* _clk_pwm_ctl_reg = nullptr;
        volatile ClockDivider* _clk_pwm_div_reg = nullptr;

        AddressSpaceInfo _asi;
};
