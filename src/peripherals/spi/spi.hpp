#pragma once

#include <cstdint>

#include "peripherals/peripheral.hpp"
#include "peripherals/spi/spi_defs.hpp"
#include "utils/reg_mem_utils.hpp"

class SPI : public Peripheral {
    public:
        SPI(uint32_t speed, SPIControlStatus init_status);
        virtual ~SPI();

        void xfer(const char* tx_buf, char* rx_buf, size_t n_bytes) const;
        void set_clock(uint32_t speed);
        void* reg_to_bus(uint32_t reg_ofs_bytes) const;

        void start_dma(
            uint32_t tx_req_thresh,
            uint32_t tx_panic_thresh,
            uint32_t rx_req_thresh,
            uint32_t rx_panic_thresh
        );
        void stop_dma();

    protected:
        uint32_t _speed;

        volatile SPIControlStatus* _cs_reg = nullptr;
        volatile uint32_t* _fifo_reg = nullptr;
        volatile uint32_t* _cdiv_reg = nullptr;
        volatile uint32_t* _dlen_reg = nullptr;
        volatile SPIDMAControl* _dc_reg = nullptr;
        SPIControlStatus _orig_cs = {};
};
