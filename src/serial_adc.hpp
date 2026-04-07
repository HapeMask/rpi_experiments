#pragma once
#include <vector>
#include <thread>
#include <semaphore>
#include <utility>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
namespace py = pybind11;

#include "peripherals/dma/dma.hpp"
#include "peripherals/gpio/gpio.hpp"
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
            int rx_block_size=32768
        );
        virtual ~SerialADC();

        uint32_t start_sampling(uint32_t sample_rate_hz) override;
        void toggle_channel(int channel_idx) override {}
        void stop_sampling() override;
        void resize(int n_samples) override;

        int n_active_channels() const override { return _logic_analyzer_mode ? _logic_analyzer_n_bits : 1; }

        void set_logic_analyzer_mode(bool enable, int n_bits = 8) override;

    protected:
        uint32_t _spi_flag_bits;

        void _run_dma();
        void _stop_dma();
        void _setup_dma_cbs();

        void _fetch_data() override;
        float _sample_to_float(uint32_t raw_sample) const;

        MemPtrs _data;
        int _rx_block_size;
        uint32_t* _tx_data_virt = nullptr;
        uint32_t* _tx_data_bus = nullptr;
        uint8_t* _rx_data_virt = nullptr;
        uint8_t* _rx_data_bus = nullptr;

        MemPtrs _la_data;
        uint32_t* _la_rx_data_virt = nullptr;
        uint32_t* _la_rx_data_bus = nullptr;

        const int _dma_chan_0 = 9;
        const int _dma_chan_1 = 10;
        const int _n_channels = 1;

        GPIO _gpio;
        SPI _spi;
        DMA _dma;
        AddressSpaceInfo _asi;
};
