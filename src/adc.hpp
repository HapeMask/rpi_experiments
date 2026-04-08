#pragma once
#include <vector>
#include <string>
#include <tuple>
#include <optional>
#include <utility>
#include <thread>
#include <mutex>
#include <atomic>
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
    py::array_t<float> _sample_bufs;  // used only for the resize() shape check
    bool _logic_analyzer_mode = false;
    int _logic_analyzer_n_bits = 8;

    // Async worker infrastructure
    std::thread       _worker_thread;
    std::atomic<bool> _running{false};
    std::mutex        _buf_mutex;
    std::vector<float> _front_data;  // latest completed data, read by get_buffers()
    std::vector<float> _back_data;   // worker writes here during _finish_fetch()

    // Allocates/zeroes both flat buffers to n_channels * n_samples * 2 floats.
    void _resize_flat_bufs(int n_channels, int n_samples);

    // Starts a background worker thread that continuously acquires data.
    // Stops any existing worker first. rate_hz is used to compute sleep duration.
    void _start_worker(double rate_hz);

    // Signals the worker to stop and blocks until it exits.
    void _stop_worker();

    void _worker_loop(double rate_hz);

    // Subclass interface replacing _fetch_data():
    //   _start_fetch()            — starts DMA hardware (returns immediately)
    //   _finish_fetch(target)     — waits for DMA completion, unpacks raw bytes
    //                               into target (flat float[n_ch * n_samples * 2])
    //   _abort_fetch()            — resets DMA/hardware (called when stopping mid-transfer)
    //   _get_sample_rate_hz()     — returns current sample rate for sleep calculation
    virtual void   _start_fetch() = 0;
    virtual void   _finish_fetch(float* target) = 0;
    virtual void   _abort_fetch() {}
    virtual double _get_sample_rate_hz() const = 0;
};
