#include "adc.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>

#include "peripherals/dma/dma_defs.hpp"
#include "peripherals/gpio/gpio_defs.hpp"
#include "peripherals/pwm/pwm_defs.hpp"

ADC::ADC(std::pair<float, float> vref, int n_samples, int n_channels) :
    _VREF(vref),
    _n_samples(n_samples),
    _n_channels(n_channels)
{
    _active_channels.resize(n_channels);
    for (int ch = 0; ch < n_channels; ++ch) {
        _active_channels[ch] = false;
    }
}

ADC::~ADC() {
    _la_free_buf();
}

void ADC::_resize_flat_bufs(int n_channels, int n_samples) {
    _front_bufs = py::array_t<float>({n_channels, n_samples, 2});
    _back_bufs  = py::array_t<float>({n_channels, n_samples, 2});
    std::memset(_front_bufs.mutable_data(), 0, _front_bufs.nbytes());
    std::memset(_back_bufs.mutable_data(),  0, _back_bufs.nbytes());
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

        _finish_fetch(_back_bufs.mutable_data());

        {
            std::lock_guard<std::mutex> lock(_buf_mutex);
            std::swap(_front_bufs, _back_bufs);
        }
        ++_front_gen;

        _start_fetch();  // immediately queue next transfer
    }

    _abort_fetch();  // stop any DMA that was started but not yet collected
}

std::tuple<py::array_t<float>, bool, std::optional<int>> ADC::get_buffers(
    int screen_width,
    std::pair<double, double> x_range,
    bool auto_range,
    std::pair<float, float> thresh,
    TrigMode trig_mode,
    int skip_samples
) {
    const auto [x_start, x_end]     = x_range;
    const auto [low_thresh, high_thresh] = thresh;
    if (skip_samples < 0) skip_samples = 0;

    // Snapshot the latest completed buffer. Hold the lock only for the copy so
    // the worker can swap its next completed buffer while we process this one.
    int n_ch_in_buf = 0;
    py::array_t<float> snap;

    // Read _front_bufs shape and snapshot its data under one lock. The worker
    // does std::swap(_front_bufs, _back_bufs) under this same lock, and move-
    // swap briefly sets _front_bufs.m_ptr = nullptr as an intermediate step —
    // accessing it outside the lock can dereference null.
    {
        std::lock_guard<std::mutex> lock(_buf_mutex);
        if (_front_bufs.ndim() == 3) {
            n_ch_in_buf = static_cast<int>(_front_bufs.shape(0));
        }

        if (n_ch_in_buf > 0) {
            snap = py::array_t<float>({n_ch_in_buf, _n_samples, 2}, _front_bufs.data());
        }
    }

    if (skip_samples >= _n_samples || n_ch_in_buf == 0 || n_active_channels() == 0) {
        return {py::array_t<float>({n_ch_in_buf, screen_width, 2}), false, std::nullopt};
    }

    auto snap_ref = snap.unchecked<3>();

    // Trigger detection (channel 0 only)
    bool triggered = false;
    std::optional<int> trig_start = std::nullopt;

    if (trig_mode != TrigMode::NONE) {
        const int ch = 0;
        float low  = low_thresh;
        float high = high_thresh;

        if (auto_range) {
            float min_val  = snap_ref(ch, skip_samples, 0);
            float max_val  = min_val;
            float mean_val = 0;
            for (int i = skip_samples + 1; i < _n_samples; ++i) {
                const float v = snap_ref(ch, i, 0);
                min_val   = std::min(min_val, v);
                max_val   = std::max(max_val, v);
                mean_val += v;
            }
            mean_val /= (_n_samples - skip_samples);
            const float range = max_val - min_val;
            low  = mean_val - 0.2f * range;
            high = mean_val + 0.2f * range;
        }

        if (trig_mode == TrigMode::RISING_EDGE) {
            for (int i = skip_samples; i < _n_samples; ++i) {
                const float v = snap_ref(ch, i, 0);
                if (v < low) trig_start = i;
                if (v >= high && trig_start.has_value()) { triggered = true; break; }
            }
        } else if (trig_mode == TrigMode::FALLING_EDGE) {
            for (int i = skip_samples; i < _n_samples; ++i) {
                const float v = snap_ref(ch, i, 0);
                if (v > high) trig_start = i;
                if (v <= low && trig_start.has_value()) { triggered = true; break; }
            }
        }
    }

    // Time origin: sample index at t=0 (trigger point if triggered, else 0)
    const double sample_rate = _get_sample_rate_hz();
    const double trigger_origin = (triggered && trig_start.has_value())
        ? static_cast<double>(*trig_start)
        : 0.0;

    // Convert visible time window to sample indices.
    // If x_end <= x_start (e.g. default -1), show the full valid range.
    int win_start, win_end;
    if (x_end <= x_start) {
        win_start = skip_samples;
        win_end   = _n_samples;
    } else {
        win_start = static_cast<int>(std::round(x_start * sample_rate + trigger_origin));
        win_end   = static_cast<int>(std::round(x_end   * sample_rate + trigger_origin));
        win_start = std::max(win_start, skip_samples);
        win_end   = std::min(win_end, _n_samples);
    }

    if (win_start >= win_end) {
        return {py::array_t<float>({n_ch_in_buf, screen_width, 2}), triggered, trig_start};
    }

    // Bin win_start..win_end into screen_width bins. Timestamps are in seconds,
    // relative to the trigger point (or to sample 0 if not triggered).
    py::array_t<float> binned_bufs({n_ch_in_buf, screen_width, 2});
    auto bbuf = binned_bufs.mutable_unchecked<3>();

    const int win_size = win_end - win_start;
    const float bins_to_samples = static_cast<float>(win_size) / screen_width;

    for (int b = 0; b < screen_width; ++b) {
        int s_start = win_start + static_cast<int>(b * bins_to_samples);
        int s_end   = win_start + static_cast<int>((b + 1) * bins_to_samples);
        if (s_end > win_end) s_end = win_end;
        if (s_start >= s_end) s_start = std::max(win_start, s_end - 1);

        const int count = s_end - s_start;
        const float bin_time = static_cast<float>(
            (static_cast<double>(s_start + s_end) * 0.5 - trigger_origin) / sample_rate
        );

        for (int ch = 0; ch < n_ch_in_buf; ++ch) {
            float sum_val = 0;
            for (int i = s_start; i < s_end; ++i) {
                sum_val += snap_ref(ch, i, 0);
            }
            bbuf(ch, b, 0) = sum_val / count;
            bbuf(ch, b, 1) = bin_time;
        }
    }

    return {binned_bufs, triggered, trig_start};
}

bool ADC::channel_active(int ch) const {
    return _active_channels[ch];
}
