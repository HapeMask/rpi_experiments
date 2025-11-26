#pragma once

// Make/include the appropriate header for your device. TODO: Can these also be
// determined programmatically??
#include "utils/rpi_zero_2.hpp"

#include "peripherals/peripheral.hpp"
#include "peripherals/pwm/pwm_defs.hpp"
#include "peripherals/clock/clock.hpp"
#include "utils/reg_mem_utils.hpp"


class PWM : public Peripheral {
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

    protected:
        float _duty_cycle;
        float _freq;
        bool _use_m_s = true;
        bool _use_fifo;

        volatile PWMControl* _ctl_reg = nullptr;
        volatile PWMStatus* _sta_reg = nullptr;
        volatile PWMDMAControl* _dmac_reg = nullptr;
        volatile uint32_t* _dat_reg = nullptr;
        volatile uint32_t* _rng_reg = nullptr;
        volatile uint32_t* _fif_reg = nullptr;

        Clock _clock;
};
