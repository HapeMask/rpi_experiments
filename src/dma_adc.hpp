#pragma once
#include <vector>
#include <thread>
#include <semaphore>

#include "peripherals/dma/dma.hpp"
#include "peripherals/gpio/gpio.hpp"
#include "peripherals/spi/spi.hpp"
#include "peripherals/spi/spi_defs.hpp"
#include "utils/reg_mem_utils.hpp"
#include "utils/rpi_zero_2.hpp"

class DMAADC {
    public:
        DMAADC(
            int spi_speed,
            uint32_t spi_flag_bits,
            float vdd,
            int n_samples=16384,
            int rx_block_size=32768
        );
        virtual ~DMAADC();

        void start_sampling();
        void stop_sampling();
        void resize(int n_samples);

        std::tuple<std::vector<float>, std::vector<float>> get_buffers();
        float VDD() const { return _VDD; }
        int n_samples() const { return _sample_buf.size(); }

    protected:
        float _timescale;

        void _run_dma();
        void _stop_dma();
        void _setup_dma_cbs();

        MemPtrs _data;
        int _rx_block_size;
        uint32_t* _tx_data_virt = nullptr;
        uint32_t* _tx_data_bus = nullptr;
        uint8_t* _rx_data_virt = nullptr;
        uint8_t* _rx_data_bus = nullptr;

        const int _dma_chan_0 = 9;
        const int _dma_chan_1 = 10;

        GPIO _gpio;
        SPI _spi;
        DMA _dma;
        AddressSpaceInfo _asi;

        std::vector<float> _sample_buf;
        std::vector<float> _ts_buf;
        float _VDD;
};
