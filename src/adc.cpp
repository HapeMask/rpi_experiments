#include "adc.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>

#include "peripherals/dma/dma_defs.hpp"
#include "peripherals/gpio/gpio_defs.hpp"
#include "peripherals/pwm/pwm_defs.hpp"

ADC::~ADC() {
    _la_free_buf();
}

void ADC::_resize_flat_bufs(int n_channels, int n_samples) {
    const size_t sz = (size_t)n_channels * n_samples * 2;
    _front_data.assign(sz, 0.f);
    _back_data.assign(sz, 0.f);
}

// ---- LA buffer management -----------------------------------------------

void ADC::_la_alloc_buf(int n_samples) {
    const int n_bytes = n_samples * sizeof(uint32_t);
    if (_dma._use_vc_mem) {
        _la_data = _dma._mbox.alloc_vc_mem(n_bytes, _asi.page_size);
    } else {
        _la_data.virt = alloc_locked_block(n_bytes, _asi.page_size);
        _la_data.phys = virt_to_phys(_la_data.virt, _asi.page_size);
        _la_data.bus  = _asi.phys_to_bus(_la_data.phys);
    }
    _la_rx_data_virt = (uint32_t*)_la_data.virt;
    _la_rx_data_bus  = (uint32_t*)_la_data.bus;
}

void ADC::_la_free_buf() {
    if (_dma._use_vc_mem && _la_data.vc_handle) {
        _dma._mbox.free_vc_mem(_la_data);
    } else if (_la_data.virt) {
        free(_la_data.virt);
    }
    _la_data         = {};
    _la_rx_data_virt = nullptr;
    _la_rx_data_bus  = nullptr;
}

// ---- LA DMA CB setup -------------------------------------------------------

void ADC::_setup_la_dma_cbs() {
    _dma.resize_cbs(2 * _n_samples);

    const auto gpio_lev0_bus_addr = (uint32_t)(uintptr_t)_gpio.reg_to_bus(GPIO_LVL_OFS);
    const auto pwm_fifo_bus_addr  = (uint32_t)(uintptr_t)_pwm.reg_to_bus(PWM0_FIF_OFS);

    // Two CBs per sample: cb_pwm waits for PWM DREQ then writes a dummy word to
    // the PWM FIFO (consuming the token), then cb_gpio immediately reads GPIO.
    for (int i = 0; i < _n_samples; ++i) {
        auto& cb_pwm  = _dma.get_cb(2 * i);
        auto& cb_gpio = _dma.get_cb(2 * i + 1);

        cb_pwm.ti = DMATransferInfo{{
            .wait_for_writes=1, .dest_dma_req=1,
            .src_ignore_reads=1, .peri_map=DMA_PERI_MAP_PWM
        }}.bits;
        cb_pwm.src     = 0;
        cb_pwm.dst     = pwm_fifo_bus_addr;
        cb_pwm.len     = 4;
        cb_pwm.next_cb = (uint32_t)(uintptr_t)_dma.get_cb_bus_ptr(2 * i + 1);

        cb_gpio.ti = DMATransferInfo{{.wait_for_writes=1}}.bits;
        cb_gpio.src    = gpio_lev0_bus_addr;
        cb_gpio.dst    = (uint32_t)(uintptr_t)(_la_rx_data_bus + i);
        cb_gpio.len    = 4;
        cb_gpio.next_cb = (i < _n_samples - 1)
            ? (uint32_t)(uintptr_t)_dma.get_cb_bus_ptr(2 * i + 2) : 0;
    }
}

// ---- LA sampling / fetch ---------------------------------------------------

void ADC::_la_start_sampling(uint32_t rate_hz) {
    _pwm.setup_clock(0.5f, (float)rate_hz, ClockSource::PLLD);
    _pwm.enable_dma();
    _start_worker(rate_hz);
}

void ADC::_start_la_fetch() {
    _pwm.start();
    _dma.start(_la_dma_chan, /*first_cb_idx=*/0);
}

void ADC::_finish_la_fetch(float* target) {
    const int rate_hz = static_cast<int>(_get_sample_rate_hz());
    // Break up the wait into chunks to balance sleeping vs. finishing on time.
    _dma.wait(
        _la_dma_chan,
        16,
        std::max(1000000 * _n_samples / (8 * rate_hz), 1)
    );

    _dma.reset(_la_dma_chan);
    _pwm.stop();

    for (int bit = 0; bit < _logic_analyzer_n_bits; ++bit) {
        for (int i = 0; i < _n_samples; ++i) {
            const uint32_t gpio_word = _la_rx_data_virt[i];
            target[bit * _n_samples * 2 + i * 2 + 0] =
                ((gpio_word >> (8 + bit)) & 1) ? 1.0f : 0.0f;
            target[bit * _n_samples * 2 + i * 2 + 1] = static_cast<float>(i);
        }
    }
}

void ADC::_abort_la_fetch() {
    _pwm.stop();
    _dma.reset(_la_dma_chan);
}

// ---- LA resize helper ------------------------------------------------------

void ADC::_la_resize(int n_samples) {
    _la_free_buf();
    _la_alloc_buf(n_samples);
    _sample_bufs = py::array_t<float>(
        {_logic_analyzer_n_bits, n_samples, 2},
        {n_samples * 2 * sizeof(float), 2 * sizeof(float), sizeof(float)}
    );
    _resize_flat_bufs(_logic_analyzer_n_bits, n_samples);
    _setup_la_dma_cbs();
}

// ---- set_logic_analyzer_mode -----------------------------------------------

void ADC::set_logic_analyzer_mode(bool enable, int n_bits) {
    if (enable && n_bits != 8 && n_bits != 16)
        throw std::runtime_error("n_bits must be 8 or 16");

    _stop_worker();
    _la_free_buf();

    _logic_analyzer_mode    = enable;
    _logic_analyzer_n_bits  = enable ? n_bits : 8;

    if (enable) {
        _la_alloc_buf(_n_samples);
        _sample_bufs = py::array_t<float>(
            {n_bits, _n_samples, 2},
            {_n_samples * 2 * sizeof(float), 2 * sizeof(float), sizeof(float)}
        );
        _resize_flat_bufs(n_bits, _n_samples);
        _setup_la_dma_cbs();
    } else {
        _on_la_mode_exit();
    }
}

// ---- worker ----------------------------------------------------------------

void ADC::_start_worker(double rate_hz) {
    _stop_worker();  // join any existing worker before starting a new one
    _running = true;
    _worker_thread = std::thread(&ADC::_worker_loop, this, rate_hz);
}

void ADC::_stop_worker() {
    _running = false;
    if (_worker_thread.joinable()) {
        _worker_thread.join();
    }
}

void ADC::_worker_loop(double rate_hz) {
    _start_fetch();

    while (_running) {
        // Sleep for most of the expected transfer duration so _dma.wait() finds
        // the transfer already complete rather than busy-waiting the whole time.
        const double expected_s = static_cast<double>(_n_samples) / rate_hz;
        const double chunk_s    = 0.005;  // check _running every 5 ms
        double remaining_s = expected_s * 0.9;
        while (_running && remaining_s > 0.0) {
            const double sleep_s = std::min(chunk_s, remaining_s);
            std::this_thread::sleep_for(std::chrono::duration<double>(sleep_s));
            remaining_s -= sleep_s;
        }

        if (!_running) break;  // abort before collecting; DMA cleaned up below

        _finish_fetch(_back_data.data());

        {
            std::lock_guard<std::mutex> lock(_buf_mutex);
            std::swap(_front_data, _back_data);
        }
        ++_front_gen;

        _start_fetch();  // immediately queue next transfer
    }

    _abort_fetch();  // stop any DMA that was started but not yet collected
}

std::tuple<py::array_t<float>, bool, std::optional<int>> ADC::get_buffers(
    int screen_width,
    bool auto_range,
    float low_thresh,
    float high_thresh,
    TrigMode trig_mode,
    int skip_samples
) {
    if (skip_samples < 0) skip_samples = 0;

    const int n_samp = _n_samples;
    const int n_ch_stored = (
        (n_samp > 0 && !_front_data.empty()) ?
        static_cast<int>(_front_data.size() / (static_cast<size_t>(n_samp) * 2))
        : 0
    );

    if (skip_samples >= n_samp || n_ch_stored == 0 || n_active_channels() == 0) {
        return {py::array_t<float>({n_ch_stored, screen_width, 2}), false, std::nullopt};
    }

    // Snapshot the latest completed buffer. Hold the lock only for the copy so
    // the worker can swap its next completed buffer while we process this one.
    std::vector<float> snap;
    {
        std::lock_guard<std::mutex> lock(_buf_mutex);
        snap = _front_data;
    }

    // Flat accessor for shape [n_ch, n_samp, 2]
    auto at = [&](int ch, int i, int field) -> float {
        return snap[static_cast<size_t>(ch) * n_samp * 2 + i * 2 + field];
    };

    // Trigger logic (channel 0 only)
    bool triggered = false;
    std::optional<int> trig_start = std::nullopt;

    if (trig_mode != TrigMode::NONE) {
        const int ch = 0;
        float low  = low_thresh;
        float high = high_thresh;

        if (auto_range) {
            float min_val  = at(ch, skip_samples, 0);
            float max_val  = min_val;
            float mean_val = 0;
            for (int i = skip_samples + 1; i < n_samp; ++i) {
                const float v = at(ch, i, 0);
                min_val   = std::min(min_val, v);
                max_val   = std::max(max_val, v);
                mean_val += v;
            }
            mean_val /= (n_samp - skip_samples);
            const float range = max_val - min_val;
            low  = mean_val - 0.2f * range;
            high = mean_val + 0.2f * range;
        }

        if (trig_mode == TrigMode::RISING_EDGE) {
            for (int i = skip_samples; i < n_samp; ++i) {
                const float v = at(ch, i, 0);
                if (v < low) trig_start = i;
                if (v >= high && trig_start.has_value()) { triggered = true; break; }
            }
        } else if (trig_mode == TrigMode::FALLING_EDGE) {
            for (int i = skip_samples; i < n_samp; ++i) {
                const float v = at(ch, i, 0);
                if (v > high) trig_start = i;
                if (v <= low && trig_start.has_value()) { triggered = true; break; }
            }
        }
    }

    // Binning: downsample to screen_width bins
    py::array_t<float> binned_bufs({n_ch_stored, screen_width, 2});
    auto bbuf = binned_bufs.mutable_unchecked<3>();

    const float samples_per_bin = static_cast<float>(n_samp) / screen_width;

    for (int b = 0; b < screen_width; ++b) {
        int start = static_cast<int>(b * samples_per_bin);
        int end   = static_cast<int>((b + 1) * samples_per_bin);
        if (end > n_samp) end = n_samp;
        if (start >= end) start = std::max(0, end - 1);

        const int count = end - start;
        for (int ch = 0; ch < n_ch_stored; ++ch) {
            float sum_val = 0, sum_ts = 0;
            for (int i = start; i < end; ++i) {
                sum_val += at(ch, i, 0);
                sum_ts  += at(ch, i, 1);
            }
            bbuf(ch, b, 0) = sum_val / count;
            bbuf(ch, b, 1) = sum_ts  / count;
        }
    }

    std::optional<int> binned_trig_start = std::nullopt;
    if (trig_start.has_value()) {
        binned_trig_start = static_cast<int>(*trig_start / samples_per_bin);
    }

    return {binned_bufs, triggered, binned_trig_start};
}
