#include <iostream>
#include <cstring>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <pthread.h>
#include <sched.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "dma_adc.hpp"
#include "peripherals/gpio/gpio.hpp"
#include "peripherals/spi/spi.hpp"
#include "peripherals/spi/spi_defs.hpp"
#include "utils/reg_mem_utils.hpp"
#include "utils/rpi_zero_2.hpp"

DMAADC::DMAADC(int spi_speed, uint32_t spi_flag_bits, float vdd, int n_samples, int rx_block_size) :
    _rx_block_size(rx_block_size),
    _spi(spi_speed, {.bits=spi_flag_bits}),
    _VDD(vdd)
{
    resize(n_samples);
    _timescale = 1.f / (float)OSC_CLK_HZ;

    _gpio.set_mode(SPI0_GPIO_CE0, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_CE1, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_SCLK, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_MISO, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_MOSI, GPIOMode::ALT_0);
}

DMAADC::~DMAADC() {
    if (_dma._use_vc_mem && _data.vc_handle) {
        _dma._mbox.free_vc_mem(_data);
    } else if (_data.virt) {
        free(_data.virt);
    }
}

void DMAADC::resize(int n_samples) {
    _sample_buf.resize(n_samples);
    _ts_buf.resize(n_samples);

    // 3 32-bit values for the static transmit data + 16 bits per sample.
    const int n_locked_bytes = 3 * sizeof(uint32_t) + n_samples * sizeof(uint16_t);

    if (_dma._use_vc_mem) {
        _data = _dma._mbox.alloc_vc_mem(
            n_locked_bytes,
            _asi.page_size
        );
    } else {
        _data.virt = alloc_locked_block(n_locked_bytes, _asi.page_size);
        _data.phys = virt_to_phys(_data.virt, _asi.page_size);
        _data.bus = _asi.phys_to_bus(_data.phys);
    }

    _tx_data_virt = (uint32_t*)_data.virt;
    _tx_data_bus = (uint32_t*)_data.bus;
    _rx_data_virt = (uint8_t*)(_tx_data_virt + 3);
    _rx_data_bus = (uint8_t*)(_tx_data_bus + 3);

    _setup_dma_cbs();
}

void DMAADC::_setup_dma_cbs() {
    const int n_samples = _sample_buf.size();
    const int n_rx_cbs = ((2 * n_samples) + (_rx_block_size - 1)) / _rx_block_size;

    if ((2 * n_samples) & 0xffff0000) {
        throw std::runtime_error("Requested too many samples for a single DMA transaction. Max: 32767");
    }

    _dma.resize_cbs(2 + n_rx_cbs);

    auto& cb0 = _dma.get_cb(0);
    auto& cb1 = _dma.get_cb(1);

    auto spi_fifo_bus_addr = (uint32_t)(uintptr_t)_spi.reg_to_bus(SPI_FIFO_OFS);

    // Send SPI setup word and initial CS high bits, then switch to loop on CB1 toggling CS.
    cb0.ti = DMATransferInfo{{.wait_for_writes=1, .dest_dma_req=1, .src_addr_incr=1, .peri_map=DMA_PERI_MAP_SPI_TX}}.bits;
    cb0.src = (uint32_t)(uintptr_t)_tx_data_bus;
    cb0.dst = spi_fifo_bus_addr;
    cb0.len = 4 + 4;
    cb0.next_cb = (uint32_t)(uintptr_t)_dma.get_cb_bus_ptr(1);

    _tx_data_virt[0] = (
        (2 * n_samples) << 16
        | (SPIControlStatus{{.clk_pha=1, .xfer_active=1}}.bits & 0xff)
    );
    _tx_data_virt[1] = 0b11111111111111111111111111111111;


    // Toggle CS in a loop. 
    cb1.ti = DMATransferInfo{{.wait_for_writes=1, .dest_dma_req=1, .src_addr_incr=0, .peri_map=DMA_PERI_MAP_SPI_TX}}.bits;
    cb1.src = (uint32_t)(uintptr_t)(_tx_data_bus + 2);
    cb1.dst = spi_fifo_bus_addr;
    cb1.len = 4;
    cb1.next_cb = (uint32_t)(uintptr_t)_dma.get_cb_bus_ptr(1);

    _tx_data_virt[2] = 0b00000001000000000000000100000000;

    uint8_t* cur_rx_ptr = _rx_data_bus;
    int rx_bytes_rem = 2 * n_samples;
    for(int i=2; i<2 + n_rx_cbs; ++i) {
        auto& cbi = _dma.get_cb(i);
        cbi.ti = DMATransferInfo{{.wait_for_writes=1, .dest_addr_incr=1, .src_dma_req=1, .peri_map=DMA_PERI_MAP_SPI_RX}}.bits;
        cbi.src = spi_fifo_bus_addr;
        cbi.dst = (uint32_t)(uintptr_t)cur_rx_ptr;
        cbi.len = std::min(_rx_block_size, rx_bytes_rem);

        if (i < (n_rx_cbs + 2 - 1)) {
            cbi.next_cb = (uint32_t)(uintptr_t)_dma.get_cb_bus_ptr(i + 1);
        } else {
            cbi.next_cb = 0;
        }

        cur_rx_ptr += _rx_block_size;
        rx_bytes_rem -= _rx_block_size;
    }
}

void DMAADC::_run_dma() {
    _spi.start_dma(4, 8, 4, 8);
    _dma.start(_dma_chan_0, /*first_cb_idx=*/0);
    _dma.start(_dma_chan_1, /*first_cb_idx=*/2);
    _dma.wait(_dma_chan_1);
}

void DMAADC::_stop_dma() {
    _dma.reset(_dma_chan_0);
    _dma.reset(_dma_chan_1);
    _dma.disable(_dma_chan_0);
    _dma.disable(_dma_chan_1);
    _spi.stop_dma();
}

void DMAADC::start_sampling() {
}

void DMAADC::stop_sampling() {
}

std::tuple<std::vector<float>, std::vector<float>> DMAADC::get_buffers() {
    //const auto start = read_cntvct_el0();
    _run_dma();
    //const auto end = read_cntvct_el0();
    _stop_dma();
    //const float elapsed = (float)(end - start) * _timescale;

    for(size_t i=0; i<_ts_buf.size(); ++i) {
        //const float t = (float)i / (_ts_buf.size() - 1);
        //_ts_buf[i] = t * elapsed;
        _ts_buf[i] = i;

        _sample_buf[i] = (
            _VDD * (float)(
                ((uint32_t)_rx_data_virt[2 * i + 0] << 4) |
                ((uint32_t)_rx_data_virt[2 * i + 1] >> 4)
            ) / 1024.f
        );
    }
    return {_sample_buf, _ts_buf};
}
