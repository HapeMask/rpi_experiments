#include <cmath>
#include <tuple>
#include <utility>

#include "parallel_adc.hpp"
#include "peripherals/gpio/gpio_defs.hpp"
#include "utils/rpi_zero_2.hpp"


ParallelADC::ParallelADC(std::pair<float, float> vref, int n_samples, int n_channels) :
    _VREF(vref),
    _n_channels(n_channels)
{
    if (n_channels < 1 || n_channels > 2) {
        throw std::runtime_error("Only 1 or 2 channels are supported.");
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

    // TODO: Add the remaining 8 lines when we have 2 channels for real.

    _active_channels.resize(n_channels);
    for (int ch=0; ch < n_channels; ++ch) {
        _active_channels[ch] = false;
    }

    resize(n_samples);
}

ParallelADC::~ParallelADC() {
    _dma._mbox.free_vc_mem(_data);
    _data.vc_handle = 0;
    _data.virt = nullptr;
    _data.phys = nullptr;
    _data.bus = nullptr;
}

void ParallelADC::resize(int n_samples) {
    if (n_samples == _n_samples) {
        return;
    }

    _n_samples = n_samples;

    _sample_bufs = py::array_t<float>(
        {_n_channels, _n_samples, 2},
        {_n_samples * 2 * sizeof(float), 2 * sizeof(float), sizeof(float)}
    );
    _sample_bufs[py::ellipsis()] = 0.f;

    if (_data.virt != nullptr) {
        _dma._mbox.free_vc_mem(_data);
    }

    _data = _dma._mbox.alloc_vc_mem(n_samples * sizeof(uint16_t), _asi.page_size);
    _rx_data_virt = (uint16_t*)_data.virt;
    _rx_data_bus = (uint16_t*)_data.bus;

    _setup_dma_cbs();
}

void ParallelADC::_setup_dma_cbs() {
    int bytes_to_xfer = (
        (_highest_active_channel() == 0) ?
        (_n_samples * sizeof(uint8_t)) :
        (_n_samples * sizeof(uint16_t))
    );

    // If only one channel is active (and thus 2 8-bit samples are packed into
    // each 16-bit element), we have to make sure DMA reads an even number of
    // bytes. This is because SMI will pack the last sample into the upper 8
    // bits of the last 16-bit element. If DMA only reads the requested
    // number of bytes, it will miss the last sample.
    if (_highest_active_channel() == 0) {
        bytes_to_xfer += (bytes_to_xfer % 2);
    }

    if (bytes_to_xfer > 65536) {
        throw std::runtime_error(
            "Requested too many samples for a single DMA transaction. Max: 65535 bytes."
        );
    }

    _dma.resize_cbs(1);

    auto& cb0 = _dma.get_cb(0);

    auto smi_data_bus_addr = (uint32_t)(uintptr_t)_smi.reg_to_bus(SMI_DATA_OFS);

    cb0.ti = DMATransferInfo{{.dest_addr_incr=1, .src_dma_req=1, .peri_map=DMA_PERI_MAP_SMI}}.bits;
    cb0.src = smi_data_bus_addr;
    cb0.dst = (uint32_t)(uintptr_t)_rx_data_bus;
    cb0.len = bytes_to_xfer;
    cb0.next_cb = 0;
}

uint32_t ParallelADC::start_sampling(uint32_t sample_rate_hz) {
    if (_cur_real_sample_rate != sample_rate_hz) {
        _cur_real_sample_rate = _smi.setup_timing(sample_rate_hz, ClockSource::PLLD);
    }

    SMIWidth width = (_highest_active_channel() < 1) ? SMIWidth::_8_BITS : SMIWidth::_16_BITS;
    _smi.setup_device_settings(width, /*device_id=*/0, /*use_dma=*/true);

    return _cur_real_sample_rate;
}

void ParallelADC::stop_sampling() {
}

int ParallelADC::_highest_active_channel() const {
    int highest_active_channel = -1;
    for (int i=0; i < _n_channels; ++i) {
        if (_active_channels[i]) {
            highest_active_channel = i;
        }
    }
    return highest_active_channel;
}

void ParallelADC::toggle_channel(int channel_idx) {
    const auto highest_pre = _highest_active_channel();

    _active_channels[channel_idx] = !_active_channels[channel_idx];

    SMIWidth width = (_highest_active_channel() < 1) ? SMIWidth::_8_BITS : SMIWidth::_16_BITS;
    _smi.setup_device_settings(width, /*device_id=*/0, /*use_dma=*/true);

    // If we changed SMI read width because of the channel change, we need to
    // also reset the DMA CBs to read the new number of total bytes.
    if (_highest_active_channel() != highest_pre) {
        _setup_dma_cbs();
    }
}

int ParallelADC::n_active_channels() const {
    int n_act = 0;
    for (const auto& act : _active_channels) { n_act += int(act); }
    return n_act;
}

float ParallelADC::_sample_to_float(uint8_t raw_sample) const {
    return _VREF.first + (_VREF.second - _VREF.first) * ((float)raw_sample / 255.f);
}

std::tuple<py::array_t<float>, bool, std::optional<int>> ParallelADC::get_buffers(
    bool auto_trig,
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

    _smi.start_xfer(_n_samples, /*packed=*/true);
    _dma.start(_dma_chan_0, /*first_cb_idx=*/0);
    _dma.wait(_dma_chan_0);
    _smi.stop_xfer();

    // If only the first channel is active, each uint16_t contains two packed
    // samples, swapped because SMI things (see doc PDF re:XRGB).
    auto sbuf = _sample_bufs.mutable_unchecked<3>();
    if (_highest_active_channel() == 0) {
        for (int i=0; i < (_n_samples + 1) / 2; ++i) {
            sbuf(0, 2 * i + 0, 0) = _sample_to_float((_rx_data_virt[i] >> 8) & 0xff);
            sbuf(0, 2 * i + 0, 1) = 2 * i + 0;

            if ((2 * i + 1) < _n_samples) {
                sbuf(0, 2 * i + 1, 0) = _sample_to_float((_rx_data_virt[i] >> 0) & 0xff);
                sbuf(0, 2 * i + 1, 1) = 2 * i + 1;
            }
        }
    } else {
        for (int i=0; i < _n_samples; ++i) {
            for (int ch = 0; ch < _n_channels; ++ch) {
                if (!_active_channels[ch]) {
                    continue;
                }
                const uint32_t shift = 8 * ch;

                sbuf(ch, i, 0) = _sample_to_float((_rx_data_virt[i] >> shift) & 0xff);
                sbuf(ch, i, 1) = i;
            }
        }
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

    if (auto_trig) {
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
            if (v <= low) {
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
            if (v >= high) {
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
