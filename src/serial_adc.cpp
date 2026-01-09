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

#include "peripherals/gpio/gpio.hpp"
#include "peripherals/spi/spi.hpp"
#include "peripherals/spi/spi_defs.hpp"
#include "serial_adc.hpp"
#include "utils/reg_mem_utils.hpp"
#include "utils/rpi_zero_2.hpp"

SerialADC::SerialADC(
    uint32_t spi_flag_bits,
    std::pair<float, float> vref,
    int n_samples,
    int rx_block_size
):
    _rx_block_size(rx_block_size),
    _spi_flag_bits(spi_flag_bits),
    _spi(8000000, {.bits=spi_flag_bits}),
    _VREF(vref)
{
    resize(n_samples);

    _gpio.set_mode(SPI0_GPIO_CE0, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_CE1, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_SCLK, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_MISO, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_MOSI, GPIOMode::ALT_0);
}

SerialADC::~SerialADC() {
    if (_dma._use_vc_mem && _data.vc_handle) {
        _dma._mbox.free_vc_mem(_data);
    } else if (_data.virt) {
        free(_data.virt);
    }
}

void SerialADC::resize(int n_samples) {
    _n_samples = n_samples;

    _sample_bufs = py::array_t<float>(
        {_n_channels, _n_samples, 2},
        {_n_samples * 2 * sizeof(float), 2 * sizeof(float), sizeof(float)}
    );
    _sample_bufs[py::ellipsis()] = 0.f;

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

void SerialADC::_setup_dma_cbs() {
    const int n_rx_cbs = ((2 * _n_samples) + (_rx_block_size - 1)) / _rx_block_size;

    if ((2 * _n_samples) & 0xffff0000) {
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
        (2 * _n_samples) << 16
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
    int rx_bytes_rem = 2 * _n_samples;
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

void SerialADC::_run_dma() {
    _spi.start_dma(4, 8, 4, 8);
    _dma.start(_dma_chan_0, /*first_cb_idx=*/0);
    _dma.start(_dma_chan_1, /*first_cb_idx=*/2);
    _dma.wait(_dma_chan_1);
}

void SerialADC::_stop_dma() {
    _dma.reset(_dma_chan_0);
    _dma.reset(_dma_chan_1);
    _dma.disable(_dma_chan_0);
    _dma.disable(_dma_chan_1);
    _spi.stop_dma();
}

uint32_t SerialADC::start_sampling(uint32_t sample_rate_hz) {
    _spi.set_clock(16 * sample_rate_hz);
    return sample_rate_hz;
}

void SerialADC::stop_sampling() {
}

float SerialADC::_sample_to_float(uint32_t raw_sample) const {
    return _VREF.first + (_VREF.second - _VREF.first) * ((float)raw_sample / 1023.f);
}

std::tuple<py::array_t<float>, bool, std::optional<int>> SerialADC::get_buffers(
    bool auto_range,
    float low_thresh,
    float high_thresh,
    std::string trig_mode,
    int skip_samples
) {
    // Safety check for skip_samples
    if (skip_samples < 0) {
        skip_samples = 0;
    }

    if (skip_samples >= _n_samples || n_active_channels() == 0) {
        return {_sample_bufs, false, std::nullopt};
    }

    _run_dma();
    _dma.reset(_dma_chan_0);
    _dma.reset(_dma_chan_1);
    _spi.stop_dma();

    auto sbuf = _sample_bufs.mutable_unchecked<3>();
    for (int i=0; i < _n_samples; ++i) {
        sbuf(0, i, 0) = _sample_to_float(
            ((uint32_t)_rx_data_virt[2 * i + 0] << 4) |
            ((uint32_t)_rx_data_virt[2 * i + 1] >> 4)
        );
        sbuf(0, i, 1) = i;
    }

    // Trigger logic
    bool triggered = false;
    std::optional<int> trig_start = std::nullopt;

    if (trig_mode == "none") {
         return {_sample_bufs, false, std::nullopt};
    }

    // Only check channel 0 for now.
    int ch = 0;

    float low = low_thresh;
    float high = high_thresh;

    if (auto_range) {
         // Calculate min/max from valid range
         float min_val = sbuf(ch, skip_samples, 0);
         float max_val = min_val;
         for (int i = skip_samples + 1; i < _n_samples; ++i) {
             const float v = sbuf(ch, i, 0);
             if (v < min_val) min_val = v;
             if (v > max_val) max_val = v;
         }

         const float range = max_val - min_val;
         low = min_val + 0.2f * range;
         high = min_val + 0.8f * range;
    }

    if (trig_mode == "rising_edge") {
        for (int i = skip_samples; i < _n_samples; ++i) {
            const float v = sbuf(ch, i, 0);
            if (v < low) {
                trig_start = i;
            }
            if (v >= high && trig_start.has_value()) {
                triggered = true;
                break;
            }
        }
    } else if (trig_mode == "falling_edge") {
        for (int i = skip_samples; i < _n_samples; ++i) {
            const float v = sbuf(ch, i, 0);
            if (v > high) {
                trig_start = i;
            }
            if (v <= low && trig_start.has_value()) {
                triggered = true;
                break;
            }
        }
    }

    return {_sample_bufs, triggered, trig_start};
}
