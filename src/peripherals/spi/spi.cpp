#include <cstdint>
#include <stdexcept>

#include "peripherals/spi/spi.hpp"
#include "peripherals/spi/spi_defs.hpp"

// Change include for your device.
#include "utils/rpi_zero_2.hpp"

SPI::SPI(uint32_t speed, SPIControlStatus init_status) :
    Peripheral(SPI_BASE_OFS, SPI_LEN),
    _speed(speed)
{
    _cs_reg = (volatile SPIControlStatus*)reg_addr(SPI_CS_OFS);
    _fifo_reg = reg_addr(SPI_FIFO_OFS);
    _cdiv_reg = reg_addr(SPI_CDIV_OFS);
    _dlen_reg = reg_addr(SPI_DLEN_OFS);
    _dc_reg = (volatile SPIDMAControl*)reg_addr(SPI_DC_OFS);

    _orig_cs.bits = _cs_reg->bits;
    _cs_reg->bits = init_status.bits;

    set_clock(speed);
}

SPI::~SPI() {
    if (_virt_regs_ptr) {
        _cs_reg->bits = _orig_cs.bits;
    }
}

void SPI::set_clock(uint32_t speed) {
    _speed = speed;

    const uint32_t cdiv = SPI_CLOCK_HZ / _speed;
    if (cdiv < 1 or cdiv > 32768) {
        throw std::runtime_error("Requested SPI clock speed out of range.");
    }

    if (!_cs_reg) {
        throw std::runtime_error("Failed to initialize SPI: MMIO not setup.");
    }

    *_cdiv_reg = cdiv;
}

void SPI::xfer(const char* tx_buf, char* rx_buf, size_t n_bytes) const {
    _cs_reg->flags.xfer_active = 1;

    size_t tx_cnt = 0;
    size_t rx_cnt = 0;
    while (tx_cnt < n_bytes || rx_cnt < n_bytes) {
        if (tx_cnt < n_bytes && _cs_reg->flags.writable) {
            if (tx_buf) {
                *_fifo_reg = tx_buf[tx_cnt];
            } else {
                *_fifo_reg = 0;
            }
            ++tx_cnt;
        }

        if (rx_cnt < n_bytes && _cs_reg->flags.readable) {
            if (rx_buf) {
                rx_buf[rx_cnt] = *_fifo_reg;
            } else {
                int junk = *_fifo_reg;
            }
            ++rx_cnt;
        }
    }

    while(!_cs_reg->flags.done) {
        // TODO: sleep?
    }

    _cs_reg->flags.xfer_active = 0;
}

void* SPI::reg_to_bus(uint32_t reg_ofs_bytes) const {
    return (void*)(_asi.bus_mmio_base + SPI_BASE_OFS + reg_ofs_bytes);
}

void SPI::start_dma(uint32_t tx_req_thresh, uint32_t tx_panic_thresh, uint32_t rx_req_thresh, uint32_t rx_panic_thresh) {
    _dc_reg->bits = SPIDMAControl{{
        .tx_req_thresh=tx_req_thresh,
        .tx_panic_thresh=tx_panic_thresh,
        .rx_req_thresh=rx_req_thresh,
        .rx_panic_thresh=rx_panic_thresh
    }}.bits;

    _cs_reg->bits |= SPIControlStatus{{.clear_tx=1, .clear_rx=1, .dma_enab=1}}.bits;

    (*_dlen_reg) = 0;
}

void SPI::stop_dma() {
    _cs_reg->flags.xfer_active = 0;
    _cs_reg->flags.dma_enab = 0;
}
