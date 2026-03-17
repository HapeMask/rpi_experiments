#pragma once
#include <cstdint>

#include "peripherals/dma/dma.hpp"
#include "peripherals/gpio/gpio.hpp"
#include "peripherals/mailbox/mailbox.hpp"
#include "peripherals/smi/smi.hpp"
#include "utils/reg_mem_utils.hpp"
#include "utils/rpi_zero_2.hpp"

class FrequencyCounter {
    public:
        FrequencyCounter(
            int tgt_sample_rate=50'000'000,
            int n_samples=16384,
            int gpio_pin=8,
            int dma_chan=10
        );
        virtual ~FrequencyCounter();

        float sample();

    protected:
        void _setup_dma_cbs();

        int _gpio_pin;
        int _n_samples;
        int _dma_chan;
        int _smi_clock_speed;

        MemPtrs _data;

        AddressSpaceInfo _asi;
        Mailbox _mbox;
        DMA _dma;
        SMI _smi;
        GPIO _gpio;
};
