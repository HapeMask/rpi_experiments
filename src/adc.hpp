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

#include "peripherals/dma/dma.hpp"
#include "peripherals/gpio/gpio.hpp"
#include "peripherals/pwm/pwm.hpp"
#include "utils/reg_mem_utils.hpp"

namespace py = pybind11;

enum class TrigMode {
    NONE,
    RISING_EDGE,
    FALLING_EDGE
};

class ADC {
public:
    ADC(std::pair<float, float> vref, int n_samples, int n_channels);
    virtual ~ADC();

    virtual uint32_t start_sampling(uint32_t sample_rate_hz) = 0;
    virtual void stop_sampling() = 0;
    virtual void resize(int n_samples) = 0;
    virtual void toggle_channel(int channel_idx) = 0;
    virtual int n_active_channels() const = 0;
    bool channel_active(int ch) const;

    virtual std::tuple<py::array_t<float>, bool, std::optional<int>> get_buffers(
        int screen_width,
        bool auto_range = false,
        float low_thresh = 0.5,
        float high_thresh = 2.5,
        TrigMode trig_mode = TrigMode::RISING_EDGE,
        int skip_samples = 0
    );

    std::pair<float, float> VREF() const { return _VREF; }
    int n_samples() const { return _n_samples; }
    int n_channels() const { return _n_channels; }
    uint64_t data_generation() const { return _front_gen.load(); }

    void set_logic_analyzer_mode(bool enable, int n_bits = 8);
    bool logic_analyzer_mode() const { return _logic_analyzer_mode; }

protected:
    std::pair<float, float> _VREF;
    int _n_samples;
    int _n_channels;
    std::vector<bool> _active_channels;

    // TODO: use this again and not the weird flat accessor helper thing.
    py::array_t<float> _sample_bufs;  // used only for the resize() shape check
    bool _logic_analyzer_mode = false;
    int _logic_analyzer_n_bits = 8;

    // Shared hardware peripherals — owned here, used by both subclasses and LA mode.
    DMA _dma;
    GPIO _gpio;
    PWM _pwm{/*use_fifo=*/true};
    AddressSpaceInfo _asi;

    // LA uses DMA channel 9 (same value as _dma_chan_0 in both subclasses, but
    // LA and non-LA modes are mutually exclusive so there is no conflict).
    static constexpr int _la_dma_chan = 9;

    // LA GPIO capture buffer
    MemPtrs   _la_data;
    uint32_t* _la_rx_data_virt = nullptr;
    uint32_t* _la_rx_data_bus  = nullptr;

    // Async worker infrastructure
    std::thread        _worker_thread;
    std::atomic<bool>  _running{false};
    std::mutex         _buf_mutex;
    std::atomic<uint64_t> _front_gen{0};
    std::vector<float> _front_data;  // latest completed data, read by get_buffers()
    std::vector<float> _back_data;   // worker writes here during _finish_fetch()

    void _resize_flat_bufs(int n_channels, int n_samples);
    void _start_worker(double rate_hz);
    void _stop_worker();
    void _worker_loop(double rate_hz);

    // LA buffer helpers
    void _la_alloc_buf(int n_samples);
    void _la_free_buf();

    // Set up the pairs of PWM-gated GPIO-read DMA CBs for _n_samples samples.
    void _setup_la_dma_cbs();

    // Called by subclass start_sampling() in LA mode after the rate is cached.
    void _la_start_sampling(uint32_t rate_hz);

    // LA fetch steps — called from subclass _start/_finish/_abort_fetch.
    void _start_la_fetch();
    void _finish_la_fetch(float* target);
    void _abort_la_fetch();

    // Re-allocates LA buffer and DMA CBs for a new sample count.
    // Subclass resize() calls this when _logic_analyzer_mode is true.
    void _la_resize(int n_samples);

    // Called by set_logic_analyzer_mode(false) so the subclass can restore its
    // non-LA buffers, _sample_bufs, and DMA CBs.
    virtual void _on_la_mode_exit() = 0;

    // Subclass data-acquisition interface
    virtual void   _start_fetch() = 0;
    virtual void   _finish_fetch(float* target) = 0;
    virtual void   _abort_fetch() {}
    virtual double _get_sample_rate_hz() const = 0;
};
