#pragma once
#include <vector>
#include <string>
#include <tuple>
#include <optional>
#include <utility>
#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>

namespace py = pybind11;

class ADC {
public:
    ADC(std::pair<float, float> vref, int n_samples) : _VREF(vref), _n_samples(n_samples) {}
    virtual ~ADC() = default;

    virtual uint32_t start_sampling(uint32_t sample_rate_hz) = 0;
    virtual void stop_sampling() = 0;
    virtual void resize(int n_samples) = 0;
    virtual void toggle_channel(int channel_idx) = 0;
    virtual int n_active_channels() const = 0;

    virtual std::tuple<py::array_t<float>, bool, std::optional<int>> get_buffers(
        int screen_width,
        bool auto_range = false,
        float low_thresh = 0.5,
        float high_thresh = 2.5,
        std::string trig_mode = "rising_edge",
        int skip_samples = 0
    );

    std::pair<float, float> VREF() const { return _VREF; }
    int n_samples() const { return _n_samples; }

    virtual void set_logic_analyzer_mode(bool enable, int n_bits = 8) {
        _logic_analyzer_mode = enable;
        _logic_analyzer_n_bits = n_bits;
    }
    bool logic_analyzer_mode() const { return _logic_analyzer_mode; }

protected:
    std::pair<float, float> _VREF;
    int _n_samples;
    py::array_t<float> _sample_bufs;
    bool _logic_analyzer_mode = false;
    int _logic_analyzer_n_bits = 8;

    virtual void _fetch_data() = 0;
};
