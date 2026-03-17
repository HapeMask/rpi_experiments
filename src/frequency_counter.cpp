#include "frequency_counter.hpp"


FrequencyCounter::FrequencyCounter(
    int gpio_pin,
    int tgt_sample_rate,
    int n_samples,
    int dma_chan
) : _gpio_pin(gpio_pin), _n_samples(n_samples), _dma_chan(dma_chan), _dma(/*n_cbs=*/2) {
    if ((n_samples % 8) != 0) {
        throw std::runtime_error(
            "n_samples must be a multiple of the SMI transfer size (1 byte / 8 bits)."
        );
    }

    if (_gpio_pin < 8 || _gpio_pin > 15) {
        throw std::runtime_error("Frequency counter uses SMI and can only use GPIO 8-15.");
    }

    // Clock
    _gpio.set_mode(6, GPIOMode::ALT_1);

    // Data lines
    _gpio.set_mode(_gpio_pin, GPIOMode::ALT_1);

    _smi_clock_speed = _smi.setup_timing(tgt_sample_rate, ClockSource::PLLD);
    _smi.setup_device_settings(SMIWidth::_8_BITS, /*device_id=*/0, /*use_dma=*/true);

    _setup_dma_cbs();
}

void FrequencyCounter::_setup_dma_cbs() {
    _data = _mbox.alloc_vc_mem(_n_samples, _asi.page_size);
    auto data_bus = (uint8_t*)_data.bus;

    auto& dma_cb = _dma.get_cb(0);

    auto smi_data_bus_addr = (uint32_t)(uintptr_t)_smi.reg_to_bus(SMI_DATA_OFS);

    dma_cb.ti = DMATransferInfo{{.dest_addr_incr=1, .src_dma_req=1, .peri_map=DMA_PERI_MAP_SMI}}.bits;
    dma_cb.src = smi_data_bus_addr;
    dma_cb.dst = (uint32_t)(uintptr_t)data_bus;
    dma_cb.len = _n_samples;
    dma_cb.next_cb = 0;
}

float FrequencyCounter::sample() {
    _smi.start_xfer(_n_samples, /*packed=*/true);
    _dma.start(_dma_chan, /*first_cb_idx=*/0);
    _dma.wait(_dma_chan);
    _smi.stop_xfer();

    auto data_virt = (uint8_t*)_data.virt;

    int last_rising_edge = -1;
    float running_conv = 0;
    float prev_running_conv = 0;
    int window_size = 3;
    float inv_window_size = 1.f / window_size;
    int period_sum = 0;
    int n_periods = 0;

    for(int i=0; i<_n_samples; ++i) {
        running_conv += inv_window_size * ((data_virt[i] & 1) ? 1.f : -1.f);

        if (i >= window_size) {
            running_conv -= inv_window_size * ((data_virt[i - window_size] & 1) ? 1.f : -1.f);

            if (prev_running_conv < 0 && running_conv > 0) {
                if (last_rising_edge != -1) {
                    period_sum += i - last_rising_edge;
                    ++n_periods;
                }
                last_rising_edge = i;
            }
            prev_running_conv = running_conv;
        }
    }

    const float mean_period = (float)period_sum / (float)n_periods;
    return _smi_clock_speed / mean_period;
}

FrequencyCounter::~FrequencyCounter() {
    _mbox.free_vc_mem(_data);
}
