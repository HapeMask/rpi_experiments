#include <cmath>
#include <tuple>
#include <utility>

#include "parallel_adc.hpp"
#include "peripherals/dma/dma_defs.hpp"
#include "peripherals/gpio/gpio_defs.hpp"
#include "peripherals/pwm/pwm_defs.hpp"
#include "utils/rpi_zero_2.hpp"


ParallelADC::ParallelADC(std::pair<float, float> vref, int n_samples, int n_channels) :
    ADC(vref, n_samples),
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
    if (_data.vc_handle) _dma._mbox.free_vc_mem(_data);
    if (_la_data.vc_handle) _dma._mbox.free_vc_mem(_la_data);
}

void ParallelADC::resize(int n_samples) {
    if (_sample_bufs.ndim() == 3 && _sample_bufs.shape(1) == n_samples) {
        _n_samples = n_samples;
        return;
    }

    _n_samples = n_samples;

    int n_first_dim = _logic_analyzer_mode ? _logic_analyzer_n_bits : _n_channels;
    _sample_bufs = py::array_t<float>(
        {n_first_dim, _n_samples, 2},
        {_n_samples * 2 * sizeof(float), 2 * sizeof(float), sizeof(float)}
    );
    _sample_bufs[py::ellipsis()] = 0.f;

    if (_logic_analyzer_mode) {
        if (_la_data.vc_handle) _dma._mbox.free_vc_mem(_la_data);
        _la_data = _dma._mbox.alloc_vc_mem(n_samples * sizeof(uint32_t), _asi.page_size);
        _la_rx_data_virt = (uint32_t*)_la_data.virt;
        _la_rx_data_bus = (uint32_t*)_la_data.bus;
    } else {
        if (_data.vc_handle) _dma._mbox.free_vc_mem(_data);
        _data = _dma._mbox.alloc_vc_mem(n_samples * sizeof(uint16_t), _asi.page_size);
        _rx_data_virt = (uint16_t*)_data.virt;
        _rx_data_bus = (uint16_t*)_data.bus;
    }

    _setup_dma_cbs();
}

void ParallelADC::_setup_dma_cbs() {
    if (_logic_analyzer_mode) {
        _dma.resize_cbs(2 * _n_samples);

        auto gpio_lev0_bus_addr = (uint32_t)(uintptr_t)_gpio.reg_to_bus(GPIO_LVL_OFS);
        auto pwm_fifo_bus_addr = (uint32_t)(uintptr_t)_pwm.reg_to_bus(PWM0_FIF_OFS);

        // Pairs of CBs per sample: cb_pwm waits for PWM DREQ then writes to FIFO
        // (consuming the DREQ token), then cb_gpio immediately reads GPIO.
        for (int i = 0; i < _n_samples; ++i) {
            auto& cb_pwm  = _dma.get_cb(2 * i);
            auto& cb_gpio = _dma.get_cb(2 * i + 1);

            cb_pwm.ti = DMATransferInfo{{
                .wait_for_writes=1, .dest_dma_req=1,
                .src_ignore_reads=1, .peri_map=DMA_PERI_MAP_PWM
            }}.bits;
            cb_pwm.src = 0;
            cb_pwm.dst = pwm_fifo_bus_addr;
            cb_pwm.len = 4;
            cb_pwm.next_cb = (uint32_t)(uintptr_t)_dma.get_cb_bus_ptr(2 * i + 1);

            cb_gpio.ti = DMATransferInfo{{.wait_for_writes=1}}.bits;
            cb_gpio.src = gpio_lev0_bus_addr;
            cb_gpio.dst = (uint32_t)(uintptr_t)(_la_rx_data_bus + i);
            cb_gpio.len = 4;
            cb_gpio.next_cb = (i < _n_samples - 1) ?
                (uint32_t)(uintptr_t)_dma.get_cb_bus_ptr(2 * i + 2) : 0;
        }
        return;
    }

    _dma.resize_cbs(1);
    auto& cb0 = _dma.get_cb(0);

    bool use_8bit = (_highest_active_channel() == 0);
    int bytes_to_xfer = use_8bit ?
        (_n_samples * sizeof(uint8_t)) :
        (_n_samples * sizeof(uint16_t));

    // If only one channel is active (and thus 2 8-bit samples are packed into
    // each 16-bit element), we have to make sure DMA reads an even number of
    // bytes. This is because SMI will pack the last sample into the upper 8
    // bits of the last 16-bit element. If DMA only reads the requested
    // number of bytes, it will miss the last sample.
    if (use_8bit) {
        bytes_to_xfer += (bytes_to_xfer % 2);
    }

    if (bytes_to_xfer > 65536) {
        throw std::runtime_error(
            "Requested too many samples for a single DMA transaction. Max: 65535 bytes."
        );
    }

    auto smi_data_bus_addr = (uint32_t)(uintptr_t)_smi.reg_to_bus(SMI_DATA_OFS);

    cb0.ti = DMATransferInfo{{.dest_addr_incr=1, .src_dma_req=1, .peri_map=DMA_PERI_MAP_SMI}}.bits;
    cb0.src = smi_data_bus_addr;
    cb0.dst = (uint32_t)(uintptr_t)_rx_data_bus;
    cb0.len = bytes_to_xfer;
    cb0.next_cb = 0;
}

uint32_t ParallelADC::start_sampling(uint32_t sample_rate_hz) {
    if (_logic_analyzer_mode) {
        _cur_real_sample_rate = sample_rate_hz;
        _pwm.setup_clock(0.5f, (float)sample_rate_hz, ClockSource::PLLD);
        _pwm.enable_dma();
        return sample_rate_hz;
    }

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
    if (_logic_analyzer_mode) return;

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
    if (_logic_analyzer_mode) return _logic_analyzer_n_bits;
    int n_act = 0;
    for (const auto& act : _active_channels) { n_act += int(act); }
    return n_act;
}

float ParallelADC::_sample_to_float(uint32_t raw_sample) const {
    return _VREF.first + (_VREF.second - _VREF.first) * ((float)raw_sample / 255.f);
}

void ParallelADC::_fetch_data() {
    auto sbuf = _sample_bufs.mutable_unchecked<3>();

    if (_logic_analyzer_mode) {
        _pwm.start();
        _dma.start(_dma_chan_0, /*first_cb_idx=*/0);
        _dma.wait(_dma_chan_0, _n_samples, 100 * 1000000 / _cur_real_sample_rate);
        _dma.reset(_dma_chan_0);
        _pwm.stop();

        for (int i = 0; i < _n_samples; ++i) {
            const uint32_t gpio_word = _la_rx_data_virt[i];
            for (int bit = 0; bit < _logic_analyzer_n_bits; ++bit) {
                sbuf(bit, i, 0) = ((gpio_word >> (8 + bit)) & 1) ? 1.0f : 0.0f;
                sbuf(bit, i, 1) = i;
            }
        }
        return;
    }

    _smi.start_xfer(_n_samples, /*packed=*/true);
    _dma.start(_dma_chan_0, /*first_cb_idx=*/0);
    _dma.wait(_dma_chan_0, _n_samples, 100 * 1000000 / _cur_real_sample_rate);
    _smi.stop_xfer();

    // If only the first channel is active, each uint16_t contains two packed
    // samples, swapped because SMI things (see doc PDF re:XRGB).
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
}

void ParallelADC::set_logic_analyzer_mode(bool enable, int n_bits) {
    if (enable && n_bits != 8 && n_bits != 16) {
        throw std::runtime_error("n_bits must be 8 or 16");
    }

    // Free any existing logic analyzer buffer
    if (_la_data.vc_handle) {
        _dma._mbox.free_vc_mem(_la_data);
        _la_data = {};
        _la_rx_data_virt = nullptr;
        _la_rx_data_bus = nullptr;
    }

    _logic_analyzer_mode = enable;
    _logic_analyzer_n_bits = enable ? n_bits : 8;

    if (enable) {
        _la_data = _dma._mbox.alloc_vc_mem(_n_samples * sizeof(uint32_t), _asi.page_size);
        _la_rx_data_virt = (uint32_t*)_la_data.virt;
        _la_rx_data_bus = (uint32_t*)_la_data.bus;

        _sample_bufs = py::array_t<float>(
            {n_bits, _n_samples, 2},
            {_n_samples * 2 * sizeof(float), 2 * sizeof(float), sizeof(float)}
        );
    } else {
        // Restore normal-mode buffer (re-use existing _data allocation if present)
        if (!_data.vc_handle) {
            _data = _dma._mbox.alloc_vc_mem(_n_samples * sizeof(uint16_t), _asi.page_size);
            _rx_data_virt = (uint16_t*)_data.virt;
            _rx_data_bus = (uint16_t*)_data.bus;
        }

        _sample_bufs = py::array_t<float>(
            {_n_channels, _n_samples, 2},
            {_n_samples * 2 * sizeof(float), 2 * sizeof(float), sizeof(float)}
        );
    }
    _sample_bufs[py::ellipsis()] = 0.f;

    _setup_dma_cbs();
}
