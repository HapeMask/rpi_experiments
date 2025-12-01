#pragma once
#include <vector>
#include <thread>
#include <semaphore>

#include "peripherals/clock/clock.hpp"
#include "peripherals/dma/dma.hpp"
#include "peripherals/gpio/gpio.hpp"
#include "peripherals/smi/smi.hpp"
#include "utils/reg_mem_utils.hpp"

class ParallelADC {
    public:
        ParallelADC(float vdd, int n_samples=16384, int n_channels=2);
        virtual ~ParallelADC();

        uint32_t start_sampling(uint32_t sample_rate_hz);
        void toggle_channel(int channel_idx);
        void stop_sampling();
        void resize(int n_samples, int n_channels);

        std::vector<std::vector<std::tuple<float, float>>> get_buffers();
        float VDD() const { return _VDD; }
        int n_samples() const { return _n_samples; }
        int n_active_channels() const;

    protected:
        float _VDD;
        int _n_samples;
        uint32_t _cur_real_sample_rate = 0;
        int _n_channels = 0;

        float _sample_to_float(uint8_t raw_sample) const;

        std::vector<bool> _active_channels;
        std::vector<std::vector<std::tuple<float, float>>> _sample_bufs;
        int _highest_active_channel() const;

        void _setup_dma_cbs();

        MemPtrs _data;
        uint16_t* _rx_data_virt = nullptr;
        uint16_t* _rx_data_bus = nullptr;

        DMA _dma;
        SMI _smi;
        GPIO _gpio;
        AddressSpaceInfo _asi;

        const int _dma_chan_0 = 9;
};
