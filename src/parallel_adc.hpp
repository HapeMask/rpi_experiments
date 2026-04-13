#pragma once
#include <vector>
#include <string>
#include <tuple>
#include <optional>
#include <utility>

#include "peripherals/dma/dma_defs.hpp"
#include "peripherals/smi/smi.hpp"
#include "utils/rpi_zero_2.hpp"

#include "adc.hpp"

class ParallelADC : public ADC {
    public:
        ParallelADC(std::pair<float, float> vref, int n_samples=16384, int n_channels=2, int bit_format=1);
        virtual ~ParallelADC();

        uint32_t start_sampling(uint32_t sample_rate_hz) override;
        void toggle_channel(int channel_idx) override;
        void stop_sampling() override;
        void resize(int n_samples) override;

        int n_active_channels() const override;

    protected:
        uint32_t _cur_real_sample_rate = 0;
        int _bit_format;

        void _start_fetch() override;
        void _finish_fetch(float* target) override;
        void _abort_fetch() override;
        double _get_sample_rate_hz() const override { return _cur_real_sample_rate; }
        void _on_la_mode_exit() override;

        float _sample_to_float(uint8_t raw_sample) const;

        int _highest_active_channel() const;

        void _setup_dma_cbs();

        MemPtrs   _data;
        uint16_t* _rx_data_virt = nullptr;
        uint16_t* _rx_data_bus  = nullptr;

        SMI _smi;

        const int _dma_chan_0 = 9;
};
