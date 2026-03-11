#include "adc.hpp"
#include <iostream>
#include <algorithm>
#include <cmath>

std::tuple<py::array_t<float>, bool, std::optional<int>> ADC::get_buffers(
    bool auto_range,
    float low_thresh,
    float high_thresh,
    std::string trig_mode,
    int skip_samples
) {
    if (skip_samples < 0) skip_samples = 0;

    int n_act = n_active_channels();
    if (skip_samples >= _n_samples || n_act == 0) {
        return {_sample_bufs, false, std::nullopt};
    }

    _fetch_data();

    auto sbuf = _sample_bufs.mutable_unchecked<3>();
    const int n_channels = _sample_bufs.shape(0);

    // Trigger logic
    bool triggered = false;
    std::optional<int> trig_start = std::nullopt;

    if (trig_mode != "none") {
        // Only check channel 0 for now.
        int ch = 0;
        float low = low_thresh;
        float high = high_thresh;

        if (auto_range) {
             float min_val = sbuf(ch, skip_samples, 0);
             float max_val = min_val;
             float mean_val = 0;
             for (int i = skip_samples + 1; i < _n_samples; ++i) {
                 const float v = sbuf(ch, i, 0);
                 min_val = std::min(min_val, v);
                 max_val = std::max(max_val, v);
                 mean_val += v;
             }
             mean_val /= _n_samples - skip_samples;

             const float range = max_val - min_val;
             low = mean_val - 0.2f * range;
             high = mean_val + 0.2f * range;
        }

        if (trig_mode == "rising_edge") {
            for (int i = skip_samples; i < _n_samples; ++i) {
                const float v = sbuf(ch, i, 0);
                if (v < low) trig_start = i;
                if (v >= high && trig_start.has_value()) {
                    triggered = true;
                    break;
                }
            }
        } else if (trig_mode == "falling_edge") {
            for (int i = skip_samples; i < _n_samples; ++i) {
                const float v = sbuf(ch, i, 0);
                if (v > high) trig_start = i;
                if (v <= low && trig_start.has_value()) {
                    triggered = true;
                    break;
                }
            }
        }
    }

    return {_sample_bufs, triggered, trig_start};
}
