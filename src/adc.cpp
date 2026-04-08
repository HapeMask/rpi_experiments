#include "adc.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <chrono>

void ADC::_resize_flat_bufs(int n_channels, int n_samples) {
    const size_t sz = (size_t)n_channels * n_samples * 2;
    _front_data.assign(sz, 0.f);
    _back_data.assign(sz, 0.f);
}

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

        if (_running) {
            _start_fetch();  // immediately queue next transfer
        }
    }

    _abort_fetch();  // stop any DMA that was started but not yet collected
}

std::tuple<py::array_t<float>, bool, std::optional<int>> ADC::get_buffers(
    int screen_width,
    bool auto_range,
    float low_thresh,
    float high_thresh,
    std::string trig_mode,
    int skip_samples
) {
    if (skip_samples < 0) skip_samples = 0;

    const int n_samp = _n_samples;
    const int n_ch_stored = (n_samp > 0 && !_front_data.empty())
        ? static_cast<int>(_front_data.size() / (static_cast<size_t>(n_samp) * 2))
        : 0;

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

    // Flat accessor: layout is [ch * n_samp * 2 + i * 2 + field]
    auto at = [&](int ch, int i, int field) -> float {
        return snap[static_cast<size_t>(ch) * n_samp * 2 + i * 2 + field];
    };

    // Trigger logic (channel 0 only)
    bool triggered = false;
    std::optional<int> trig_start = std::nullopt;

    if (trig_mode != "none") {
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

        if (trig_mode == "rising_edge") {
            for (int i = skip_samples; i < n_samp; ++i) {
                const float v = at(ch, i, 0);
                if (v < low) trig_start = i;
                if (v >= high && trig_start.has_value()) { triggered = true; break; }
            }
        } else if (trig_mode == "falling_edge") {
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
