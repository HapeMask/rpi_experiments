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

class SerialADC {
    public:
        SerialADC(
            uint32_t spi_flag_bits,
            std::pair<float, float> vref,
            int n_samples=16384,
            int rx_block_size=32768
        );
        virtual ~SerialADC();

        uint32_t start_sampling(uint32_t sample_rate_hz);
        void stop_sampling();
        void resize(int n_samples);

        std::tuple<py::array_t<float>, bool, std::optional<int>> get_buffers(
            bool auto_range,
            float low_thresh,
            float high_thresh,
            std::string trig_mode,
            int skip_samples
        );

        std::pair<float, float> VREF() const { return _VREF; }
        int n_samples() const { return _n_samples; }
        int n_active_channels() const { return 1; }
        void toggle_channel(int channel_idx) const {}

    protected:
        int _n_samples;
        uint32_t _spi_flag_bits;

        void _run_dma();
        void _stop_dma();
        void _setup_dma_cbs();

        float _sample_to_float(uint32_t raw_sample) const;

        MemPtrs _data;
        int _rx_block_size;
        uint32_t* _tx_data_virt = nullptr;
        uint32_t* _tx_data_bus = nullptr;
        uint8_t* _rx_data_virt = nullptr;
        uint8_t* _rx_data_bus = nullptr;

        const int _dma_chan_0 = 9;
        const int _dma_chan_1 = 10;
        const int _n_channels = 1;

        GPIO _gpio;
        SPI _spi;
        DMA _dma;
        AddressSpaceInfo _asi;

        py::array_t<float> _sample_bufs;
        std::pair<float, float> _VREF;
};
