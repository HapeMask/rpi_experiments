#include <cmath>
#include <tuple>
#include <utility>

#include "parallel_adc.hpp"
#include "peripherals/gpio/gpio_defs.hpp"
#include "utils/rpi_zero_2.hpp"


ParallelADC::ParallelADC(float vdd, int n_samples, int n_channels) :
    _VDD(vdd)
{
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

    resize(n_samples, n_channels);
}

ParallelADC::~ParallelADC() {
}

void ParallelADC::resize(int n_samples, int n_channels) {
    if (n_channels < 0 || n_channels > 2) {
        throw std::runtime_error("Only 1 or 2 channels are supported.");
    }

    _n_samples = n_samples;
    _n_channels = n_channels;

    _sample_bufs.resize(n_channels);
    for (int ch=0; ch < n_channels; ++ch) {
        _sample_bufs[ch].resize(n_samples);

        for (int i=0; i < n_samples; ++i) {
            _sample_bufs[ch][i] = {i, 0.f};
        }
    }
    _active_channels.resize(n_channels);
    for (int ch=0; ch < n_channels; ++ch) {
        _active_channels[ch] = false;
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
    return _VDD * ((float)raw_sample / 255.f);
}

std::vector<std::vector<std::tuple<float, float>>> ParallelADC::get_buffers() {
    if (n_active_channels() == 0) {
        return {{{}}};
    }

    _smi.start_xfer(_n_samples, /*packed=*/true);
    _dma.start(_dma_chan_0, /*first_cb_idx=*/0);
    _dma.wait(_dma_chan_0);
    _smi.stop_xfer();

    // If only the first channel is active, each uint16_t contains two packed
    // samples, swapped because SMI things (see doc PDF re:XRGB).
    if (_highest_active_channel() == 0) {
        for (size_t i=0; i < _n_samples / 2; ++i) {
            _sample_bufs[0][2 * i + 0] = {
                _sample_to_float((_rx_data_virt[i] >> 8) & 0xff),
                2 * i + 0
            };
            if ((2 * i + 1) < _n_samples) {
                _sample_bufs[0][2 * i + 1] = {
                    _sample_to_float((_rx_data_virt[i] >> 0) & 0xff),
                    2 * i + 1
                };
            }
        }
    } else {
        for (size_t i=0; i < _n_samples; ++i) {
            for (int ch = 0; ch < _n_channels; ++ch) {
                if (!_active_channels[ch]) {
                    continue;
                }

                const uint32_t shift = 8 * ch;
                _sample_bufs[ch][i] = {
                    _sample_to_float((_rx_data_virt[i] >> shift) & 0xff),
                    i
                };
            }
        }
    }

    return _sample_bufs;
}
