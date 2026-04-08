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
#include "peripherals/gpio/gpio_defs.hpp"
#include "peripherals/pwm/pwm_defs.hpp"
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
    ADC(vref, n_samples),
    _spi_flag_bits(spi_flag_bits),
    _rx_block_size(rx_block_size),
    _spi(8000000, {.bits=spi_flag_bits})
{
    resize(n_samples);

    _gpio.set_mode(SPI0_GPIO_CE0, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_CE1, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_SCLK, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_MISO, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_MOSI, GPIOMode::ALT_0);
}

SerialADC::~SerialADC() {
    _stop_worker();

    if (_dma._use_vc_mem && _data.vc_handle) {
        _dma._mbox.free_vc_mem(_data);
    } else if (_data.virt) {
        free(_data.virt);
    }
    // _la_data is freed by ~ADC()
}

void SerialADC::resize(int n_samples) {
    _stop_worker();

    if (_sample_bufs.ndim() == 3 && _sample_bufs.shape(1) == n_samples) {
        _n_samples = n_samples;
        return;
    }

    _n_samples = n_samples;

    if (_logic_analyzer_mode) {
        _la_resize(n_samples);
        return;
    }

    // 3 uint32_t words for TX (control + CS hold + CS toggle) + 2 bytes per sample for RX
    const int n_locked_bytes = 3 * sizeof(uint32_t) + n_samples * sizeof(uint16_t);
    if (_dma._use_vc_mem && _data.vc_handle) _dma._mbox.free_vc_mem(_data);
    else if (_data.virt) free(_data.virt);

    if (_dma._use_vc_mem) {
        _data = _dma._mbox.alloc_vc_mem(n_locked_bytes, _asi.page_size);
    } else {
        _data.virt = alloc_locked_block(n_locked_bytes, _asi.page_size);
        _data.phys = virt_to_phys(_data.virt, _asi.page_size);
        _data.bus  = _asi.phys_to_bus(_data.phys);
    }
    _tx_data_virt = (uint32_t*)_data.virt;
    _tx_data_bus  = (uint32_t*)_data.bus;
    _rx_data_virt = (uint8_t*)(_tx_data_virt + 3);
    _rx_data_bus  = (uint8_t*)(_tx_data_bus  + 3);

    _sample_bufs = py::array_t<float>(
        {_n_channels, _n_samples, 2},
        {_n_samples * 2 * sizeof(float), 2 * sizeof(float), sizeof(float)}
    );
    _resize_flat_bufs(_n_channels, _n_samples);

    _setup_dma_cbs();
}

// Maximum samples per SPI transaction (SPI DL field is 16-bit; 2 bytes/sample).
static constexpr int SPI_MAX_SAMPLES_PER_SEG = 32767;

void SerialADC::_setup_dma_cbs() {
    // LA mode is handled entirely by _setup_la_dma_cbs() in the base class.
    // This function only handles the SPI path.

    // Each SPI transaction is limited to SPI_MAX_SAMPLES_PER_SEG samples by the
    // 16-bit DL field. Larger captures use multiple sequential transactions; the
    // CBs here cover one segment.
    _samples_per_seg = std::min(SPI_MAX_SAMPLES_PER_SEG, _n_samples);
    const int n_rx_cbs = (2 * _samples_per_seg + _rx_block_size - 1) / _rx_block_size;

    _dma.resize_cbs(2 + n_rx_cbs);

    auto& cb0 = _dma.get_cb(0);
    auto& cb1 = _dma.get_cb(1);

    const auto spi_fifo_bus_addr = (uint32_t)(uintptr_t)_spi.reg_to_bus(SPI_FIFO_OFS);

    // CB0: write SPI control word (byte count + mode bits) and initial CS state.
    cb0.ti = DMATransferInfo{{.wait_for_writes=1, .dest_dma_req=1, .src_addr_incr=1, .peri_map=DMA_PERI_MAP_SPI_TX}}.bits;
    cb0.src     = (uint32_t)(uintptr_t)_tx_data_bus;
    cb0.dst     = spi_fifo_bus_addr;
    cb0.len     = 4 + 4;
    cb0.next_cb = (uint32_t)(uintptr_t)_dma.get_cb_bus_ptr(1);

    _tx_data_virt[0] = (
        (2 * _samples_per_seg) << 16
        | (SPIControlStatus{{.clk_pha=1, .xfer_active=1}}.bits & 0xff)
    );
    _tx_data_virt[1] = 0b11111111111111111111111111111111;

    // CB1: toggle CS in a loop to pace the transfer.
    cb1.ti = DMATransferInfo{{.wait_for_writes=1, .dest_dma_req=1, .src_addr_incr=0, .peri_map=DMA_PERI_MAP_SPI_TX}}.bits;
    cb1.src     = (uint32_t)(uintptr_t)(_tx_data_bus + 2);
    cb1.dst     = spi_fifo_bus_addr;
    cb1.len     = 4;
    cb1.next_cb = (uint32_t)(uintptr_t)_dma.get_cb_bus_ptr(1);

    _tx_data_virt[2] = 0b00000001000000000000000100000000;

    // CB2+: chain RX CBs to capture the first segment's bytes.
    uint8_t* cur_rx_ptr = _rx_data_bus;
    int rx_bytes_rem = 2 * _samples_per_seg;
    for (int i = 2; i < 2 + n_rx_cbs; ++i) {
        auto& cbi  = _dma.get_cb(i);
        cbi.ti     = DMATransferInfo{{.wait_for_writes=1, .dest_addr_incr=1, .src_dma_req=1, .peri_map=DMA_PERI_MAP_SPI_RX}}.bits;
        cbi.src    = spi_fifo_bus_addr;
        cbi.dst    = (uint32_t)(uintptr_t)cur_rx_ptr;
        cbi.len    = std::min(_rx_block_size, rx_bytes_rem);
        cbi.next_cb = (i < (n_rx_cbs + 2 - 1))
            ? (uint32_t)(uintptr_t)_dma.get_cb_bus_ptr(i + 1) : 0;

        cur_rx_ptr   += _rx_block_size;
        rx_bytes_rem -= _rx_block_size;
    }
}

// Update the TX control word and RX CB destinations for the given segment index,
// then restart both DMA channels. Called between segments in _finish_fetch().
void SerialADC::_advance_spi_segment(int seg_idx) {
    const int offset_samps = seg_idx * _samples_per_seg;
    const int seg_samps    = std::min(_samples_per_seg, _n_samples - offset_samps);
    const int n_rx_cbs     = (2 * seg_samps + _rx_block_size - 1) / _rx_block_size;

    // Update SPI byte count for this segment.
    _tx_data_virt[0] = (
        (2 * seg_samps) << 16
        | (SPIControlStatus{{.clk_pha=1, .xfer_active=1}}.bits & 0xff)
    );
    // Flush TX word from CPU cache if not using GPU-coherent memory.
    if (!_dma._use_vc_mem) {
        clean_cache(_data.virt, (uint8_t*)_data.virt + _asi.cache_line_size, _asi.cache_line_size);
    }

    // Repoint RX CBs to the correct offset in the receive buffer.
    uint8_t* rx_start_bus = _rx_data_bus + (size_t)offset_samps * 2;
    int rx_bytes_rem      = 2 * seg_samps;
    for (int i = 2; i < 2 + n_rx_cbs; ++i) {
        auto& cbi   = _dma.get_cb(i);
        cbi.dst     = (uint32_t)(uintptr_t)(rx_start_bus + (size_t)(i - 2) * _rx_block_size);
        cbi.len     = std::min(_rx_block_size, rx_bytes_rem);
        cbi.next_cb = (i < (n_rx_cbs + 2 - 1))
            ? (uint32_t)(uintptr_t)_dma.get_cb_bus_ptr(i + 1) : 0;
        rx_bytes_rem -= _rx_block_size;
    }

    _spi.start_dma(4, 8, 4, 8);
    _dma.start(_dma_chan_0, /*first_cb_idx=*/0);
    _dma.start(_dma_chan_1, /*first_cb_idx=*/2);
}

uint32_t SerialADC::start_sampling(uint32_t sample_rate_hz) {
    _sample_rate = sample_rate_hz;

    if (_logic_analyzer_mode) {
        _la_start_sampling(sample_rate_hz);
        return sample_rate_hz;
    }

    _spi.set_clock(16 * sample_rate_hz);
    _start_worker(sample_rate_hz);
    return sample_rate_hz;
}

void SerialADC::stop_sampling() {
    _stop_worker();
}

float SerialADC::_sample_to_float(uint32_t raw_sample) const {
    return _VREF.first + (_VREF.second - _VREF.first) * ((float)raw_sample / 1023.f);
}

void SerialADC::_start_fetch() {
    if (_logic_analyzer_mode) {
        _start_la_fetch();
        return;
    }

    _spi.start_dma(4, 8, 4, 8);
    _dma.start(_dma_chan_0, /*first_cb_idx=*/0);
    _dma.start(_dma_chan_1, /*first_cb_idx=*/2);
}

void SerialADC::_finish_fetch(float* target) {
    if (_logic_analyzer_mode) {
        _finish_la_fetch(target);
        return;
    }

    // Collect one or more SPI segments, each up to SPI_MAX_SAMPLES_PER_SEG samples.
    const int n_segs = (_n_samples + _samples_per_seg - 1) / _samples_per_seg;
    for (int seg = 0; seg < n_segs; ++seg) {
        const int seg_samps = std::min(_samples_per_seg, _n_samples - seg * _samples_per_seg);
        // Break up the wait into chunks to balance sleeping vs. finishing on time.
        _dma.wait(
            _dma_chan_1,
            16,
            std::max(1000000 * seg_samps / (8 * _sample_rate), 1u)
        );

        _dma.reset(_dma_chan_0);
        _dma.reset(_dma_chan_1);
        _spi.stop_dma();

        if (seg + 1 < n_segs) {
            _advance_spi_segment(seg + 1);
        }
    }

    for (int i = 0; i < _n_samples; ++i) {
        target[i * 2 + 0] = _sample_to_float(
            ((uint32_t)_rx_data_virt[2 * i + 0] << 4) |
            ((uint32_t)_rx_data_virt[2 * i + 1] >> 4)
        );
        target[i * 2 + 1] = static_cast<float>(i);
    }
}

void SerialADC::_abort_fetch() {
    if (_logic_analyzer_mode) {
        _abort_la_fetch();
        return;
    }

    _dma.reset(_dma_chan_0);
    _dma.reset(_dma_chan_1);
    _spi.stop_dma();
}

void SerialADC::_on_la_mode_exit() {
    // Re-allocate SPI buffers if they were freed when LA mode was entered.
    if (!_data.virt) {
        const int n_locked_bytes = 3 * sizeof(uint32_t) + _n_samples * sizeof(uint16_t);
        if (_dma._use_vc_mem) {
            _data = _dma._mbox.alloc_vc_mem(n_locked_bytes, _asi.page_size);
        } else {
            _data.virt = alloc_locked_block(n_locked_bytes, _asi.page_size);
            _data.phys = virt_to_phys(_data.virt, _asi.page_size);
            _data.bus  = _asi.phys_to_bus(_data.phys);
        }
        _tx_data_virt = (uint32_t*)_data.virt;
        _tx_data_bus  = (uint32_t*)_data.bus;
        _rx_data_virt = (uint8_t*)(_tx_data_virt + 3);
        _rx_data_bus  = (uint8_t*)(_tx_data_bus  + 3);
    }
    _sample_bufs = py::array_t<float>(
        {_n_channels, _n_samples, 2},
        {_n_samples * 2 * sizeof(float), 2 * sizeof(float), sizeof(float)}
    );
    _resize_flat_bufs(_n_channels, _n_samples);
    _setup_dma_cbs();
}
