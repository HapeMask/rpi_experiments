#pragma once
#include <vector>
#include <utility>

#include "peripherals/dma/dma_defs.hpp"
#include "peripherals/spi/spi.hpp"
#include "peripherals/spi/spi_defs.hpp"
#include "utils/reg_mem_utils.hpp"
#include "utils/rpi_zero_2.hpp"

#include "adc.hpp"

class SerialADC : public ADC {
    public:
        SerialADC(
            uint32_t spi_flag_bits,
            std::pair<float, float> vref,
            int n_samples=16384,
            int n_channels=1,
            int rx_block_size=32768
        );
        virtual ~SerialADC();

        uint32_t start_sampling(uint32_t sample_rate_hz) override;
        void stop_sampling() override;
        void resize(int n_samples) override;

        int n_active_channels() const override { return _logic_analyzer_mode ? _logic_analyzer_n_bits : 1; }

    protected:
        uint32_t _spi_flag_bits;
        uint32_t _sample_rate;
        int _samples_per_seg = 0;  // max samples per SPI transaction (≤ 32767)

        void _setup_dma_cbs();
        void _advance_spi_segment(int seg_idx);
        void _on_la_mode_exit() override;

        void _start_fetch() override;
        void _finish_fetch(float* target) override;
        void _abort_fetch() override;
        double _get_sample_rate_hz() const override { return _sample_rate; }

        float _sample_to_float(uint32_t raw_sample) const;

        MemPtrs   _data;
        int _rx_block_size;
        uint32_t* _tx_data_virt = nullptr;
        uint32_t* _tx_data_bus  = nullptr;
        uint8_t*  _rx_data_virt = nullptr;
        uint8_t*  _rx_data_bus  = nullptr;

        const int _dma_chan_0 = 9;
        const int _dma_chan_1 = 10;
        const int _n_channels = 1;

        SPI _spi;
};
