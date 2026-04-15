#include <cmath>
#include <tuple>
#include <utility>

#include "parallel_adc.hpp"
#include "peripherals/dma/dma_defs.hpp"
#include "peripherals/gpio/gpio_defs.hpp"
#include "peripherals/pwm/pwm_defs.hpp"
#include "utils/rpi_zero_2.hpp"


ParallelADC::ParallelADC(std::pair<float, float> vref, int n_samples, int n_channels, int bit_format) :
    ADC(vref, n_samples, n_channels),
    _bit_format(bit_format)
{
    if (n_channels < 1 || n_channels > 2) {
        throw std::runtime_error("Only 1 or 2 channels are supported.");
    }

    // Atten
    _gpio.set_mode(24, GPIOMode::OUT);
    _gpio.set_mode(25, GPIOMode::OUT);

    // Reset
    _gpio.set_mode(26, GPIOMode::OUT);

    // Initialize ADC
    _gpio.clear_pin(24);
    _gpio.clear_pin(25);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    _gpio.set_pin(24);
    _gpio.set_pin(25);
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    //_gpio.clear_pin(24);
    //_gpio.clear_pin(25);

    _gpio.set_mode(6, GPIOMode::OUT);

    _gpio.clear_pin(26);
    for(int i=0; i<4000; ++i) {
        _gpio.set_pin(6);
        std::this_thread::sleep_for(std::chrono::microseconds(10));
        _gpio.clear_pin(6);

        if (i > 10) {
            _gpio.set_pin(26);
        }

        if (i > 100) {
            _gpio.clear_pin(26);
        }
    }


    // Clock
    _gpio.set_mode(6, GPIOMode::ALT_1);

    // Data lines
    _gpio.set_mode(8, GPIOMode::ALT_1);
    _gpio.set_mode(9, GPIOMode::ALT_1);
    _gpio.set_mode(10, GPIOMode::ALT_1);
    _gpio.set_mode(11, GPIOMode::ALT_1);
    _gpio.set_mode(12, GPIOMode::ALT_1);
    _gpio.set_mode(13, GPIOMode::ALT_1);
    _gpio.set_mode(14, GPIOMode::ALT_1);
    _gpio.set_mode(15, GPIOMode::ALT_1);
    _gpio.set_mode(16, GPIOMode::ALT_1);
    _gpio.set_mode(17, GPIOMode::ALT_1);
    _gpio.set_mode(18, GPIOMode::ALT_1);
    _gpio.set_mode(19, GPIOMode::ALT_1);
    _gpio.set_mode(20, GPIOMode::ALT_1);
    _gpio.set_mode(21, GPIOMode::ALT_1);
    _gpio.set_mode(22, GPIOMode::ALT_1);
    _gpio.set_mode(23, GPIOMode::ALT_1);

    resize(n_samples);
}

ParallelADC::~ParallelADC() {
    _stop_worker();
    if (_data.vc_handle) _dma._mbox.free_vc_mem(_data);
    // _la_data is freed by ~ADC()
}

void ParallelADC::resize(int n_samples) {
    _stop_worker();

    if (_front_bufs.ndim() == 3 && _front_bufs.shape(1) == n_samples) {
        _n_samples = n_samples;
        return;
    }

    _n_samples = n_samples;

    if (_logic_analyzer_mode) {
        _la_resize(n_samples);
        return;
    }

    // Allocate enough for the worst-case transfer size (16-bit / dual-channel).
    // _setup_dma_cbs() computes the exact byte count based on current mode.
    const int alloc_bytes = n_samples * sizeof(uint16_t);
    if (_data.vc_handle) _dma._mbox.free_vc_mem(_data);
    _data = _dma._mbox.alloc_vc_mem(alloc_bytes, _asi.page_size);
    _rx_data_virt = (uint16_t*)_data.virt;
    _rx_data_bus  = (uint16_t*)_data.bus;

    _resize_flat_bufs(_n_channels, _n_samples);

    _setup_dma_cbs();
}

// Maximum bytes transferred per DMA CB (must fit in the 16-bit len field,
// and must be an even number so 8-bit packed pairs stay aligned).
static constexpr int DMA_MAX_CB_BYTES = 65534;

void ParallelADC::_setup_dma_cbs() {
    // LA mode is handled entirely by _setup_la_dma_cbs() in the base class.
    // This function only handles the SMI path.

    const bool use_8bit = (_highest_active_channel() == 0);
    int bytes_to_xfer = use_8bit ?
        (_n_samples * sizeof(uint8_t)) :
        (_n_samples * sizeof(uint16_t));

    // SMI packs the last odd 8-bit sample into the upper byte of a 16-bit word,
    // so we must transfer an even number of bytes total.
    if (use_8bit) bytes_to_xfer += (bytes_to_xfer % 2);

    const int n_cbs = (bytes_to_xfer + DMA_MAX_CB_BYTES - 1) / DMA_MAX_CB_BYTES;
    _dma.resize_cbs(n_cbs);

    // Distribute bytes evenly across all CBs so no CB is tiny. A tiny last CB
    // misses SMI DREQ: by the time the DMA loads it, the SMI transfer count has
    // reached 0 and DREQ is deasserted. Rounding up to even keeps 16-bit pairs
    // aligned across CB boundaries.
    const int max_chunk_size = ((bytes_to_xfer + n_cbs - 1) / n_cbs + 1) & ~1;

    const auto smi_data_bus_addr = (uint32_t)(uintptr_t)_smi.reg_to_bus(SMI_DATA_OFS);
    const uint32_t ti = DMATransferInfo{{.dest_addr_incr=1, .src_dma_req=1, .peri_map=DMA_PERI_MAP_SMI}}.bits;

    int remaining = bytes_to_xfer;
    uint8_t* dst_bus = (uint8_t*)_rx_data_bus;
    for (int i = 0; i < n_cbs; ++i) {
        auto& cb  = _dma.get_cb(i);
        const int chunk_size = std::min(max_chunk_size, remaining);

        cb.ti      = ti;
        cb.src     = smi_data_bus_addr;
        cb.dst     = (uint32_t)(uintptr_t)dst_bus;
        cb.len     = chunk_size;
        cb.next_cb = (
            (i < n_cbs - 1) ?
            (uint32_t)(uintptr_t)_dma.get_cb_bus_ptr(i + 1)
            : 0
        );

        dst_bus     += chunk_size;
        remaining   -= chunk_size;
    }
}

uint32_t ParallelADC::start_sampling(uint32_t sample_rate_hz) {
    if (_logic_analyzer_mode) {
        _cur_real_sample_rate = sample_rate_hz;
        _la_start_sampling(sample_rate_hz);
        return sample_rate_hz;
    }

    if (_cur_real_sample_rate != sample_rate_hz) {
        _cur_real_sample_rate = _smi.setup_timing(sample_rate_hz, ClockSource::PLLD);
    }

    SMIWidth width = (_highest_active_channel() < 1) ? SMIWidth::_8_BITS : SMIWidth::_16_BITS;
    _smi.setup_device_settings(width, /*device_id=*/0, /*use_dma=*/true);

    _start_worker(_cur_real_sample_rate);
    return _cur_real_sample_rate;
}

void ParallelADC::stop_sampling() {
    _stop_worker();
}

int ParallelADC::_highest_active_channel() const {
    int highest = -1;
    for (int i = 0; i < _n_channels; ++i) {
        if (_active_channels[i]) highest = i;
    }
    return highest;
}

void ParallelADC::toggle_channel(int channel_idx) {
    if (_logic_analyzer_mode) return;

    const bool was_running = _running.load();
    if (was_running) _stop_worker();

    const auto highest_pre = _highest_active_channel();

    _active_channels[channel_idx] = !_active_channels[channel_idx];

    SMIWidth width = (_highest_active_channel() < 1) ? SMIWidth::_8_BITS : SMIWidth::_16_BITS;
    _smi.setup_device_settings(width, /*device_id=*/0, /*use_dma=*/true);

    if (_highest_active_channel() != highest_pre) {
        _setup_dma_cbs();
    }

    if (was_running) _start_worker(_cur_real_sample_rate);
}

int ParallelADC::n_active_channels() const {
    if (_logic_analyzer_mode) return _logic_analyzer_n_bits;

    int n_act = 0;
    for (const auto& act : _active_channels) { n_act += int(act); }
    return n_act;
}

void ParallelADC::set_attenuation(bool ch1_att, bool ch2_att) {
    // HIGH = attenuation disabled, LOW = attenuation enabled
    if (ch1_att) _gpio.clear_pin(24); else _gpio.set_pin(24);
    if (ch2_att) _gpio.clear_pin(25); else _gpio.set_pin(25);
}

float ParallelADC::_sample_to_float(uint8_t raw_sample) const {
    const float sample_0_1 = (_bit_format == 0) ?
        ((float)raw_sample / 255.f) :
        (0.5f + 0.5f * (float)std::bit_cast<int8_t>(raw_sample) / 128.f);

    return _VREF.first + (_VREF.second - _VREF.first) * sample_0_1;
}

void ParallelADC::_start_fetch() {
    if (_logic_analyzer_mode) {
        _start_la_fetch();
    } else {
        _smi.start_xfer(_n_samples, /*packed=*/true);
        _dma.start(_dma_chan_0, /*first_cb_idx=*/0);
    }
}

void ParallelADC::_finish_fetch(float* target) {
    if (_logic_analyzer_mode) {
        _finish_la_fetch(target);
        return;
    }

    // Break up the wait into chunks to balance sleeping vs. finishing on time.
    _dma.wait(
        _dma_chan_0,
        16,
        std::max(1000000 * _n_samples / (8 * _cur_real_sample_rate), 1u)
    );
    _smi.stop_xfer();

    // If only the first channel is active, each uint16_t contains two packed
    // samples, swapped because of SMI XRGB packing (see SMI doc PDF).
    if (_highest_active_channel() == 0) {
        for (int i = 0; i < (_n_samples + 1) / 2; ++i) {
            target[0 * _n_samples * 2 + (2*i+0) * 2 + 0] =
                _sample_to_float((_rx_data_virt[i] >> 8) & 0xff);
            target[0 * _n_samples * 2 + (2*i+0) * 2 + 1] = static_cast<float>(2*i+0);

            if ((2*i+1) < _n_samples) {
                target[0 * _n_samples * 2 + (2*i+1) * 2 + 0] =
                    _sample_to_float((_rx_data_virt[i] >> 0) & 0xff);
                target[0 * _n_samples * 2 + (2*i+1) * 2 + 1] = static_cast<float>(2*i+1);
            }
        }
    } else {
        for (int i = 0; i < _n_samples; ++i) {
            for (int ch = 0; ch < _n_channels; ++ch) {
                if (!_active_channels[ch]) continue;
                const uint32_t shift = 8 * ch;
                target[ch * _n_samples * 2 + i * 2 + 0] =
                    _sample_to_float((_rx_data_virt[i] >> shift) & 0xff);
                target[ch * _n_samples * 2 + i * 2 + 1] = static_cast<float>(i);
            }
        }
    }
}

void ParallelADC::_abort_fetch() {
    if (_logic_analyzer_mode) {
        _abort_la_fetch();
        return;
    }

    _smi.stop_xfer();
    _dma.reset(_dma_chan_0);
}

void ParallelADC::_on_la_mode_exit() {
    // Re-allocate the SMI receive buffer if it was freed when LA mode was entered.
    if (!_data.vc_handle) {
        _data = _dma._mbox.alloc_vc_mem(_n_samples * sizeof(uint16_t), _asi.page_size);
        _rx_data_virt = (uint16_t*)_data.virt;
        _rx_data_bus  = (uint16_t*)_data.bus;
    }
    _resize_flat_bufs(_n_channels, _n_samples);
    _setup_dma_cbs();
}
