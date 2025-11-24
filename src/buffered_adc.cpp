#include <cstring>
#include <cmath>
#include <stdexcept>
#include <thread>
#include <pthread.h>
#include <sched.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "buffered_adc.hpp"
#include "peripherals/gpio/gpio.hpp"
#include "peripherals/spi/spi.hpp"
#include "peripherals/spi/spi_defs.hpp"
#include "utils/reg_mem_utils.hpp"
#include "utils/rpi_zero_2.hpp"

BufferedADC::BufferedADC(int spi_speed, uint32_t spi_flag_bits, float vdd, int n_samples) :
    _arm_timer(),
    _gpio(),
    _spi(spi_speed, {.bits=spi_flag_bits}),
    _VDD(vdd)
{
    resize(n_samples);

    set_use_arm_timer(false);

    _gpio.set_mode(SPI0_GPIO_CE0, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_CE1, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_SCLK, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_MISO, GPIOMode::ALT_0);
    _gpio.set_mode(SPI0_GPIO_MOSI, GPIOMode::ALT_0);
}

BufferedADC::~BufferedADC() {
    if (_worker_running) {
        stop_sampling();
    }
}

void BufferedADC::resize(int N) {
    _sample_buf.resize(N);
    _ts_buf.resize(N);
}

void BufferedADC::worker() {
    _worker_running = true;

    uint64_t start, now;
    size_t cur_sampler_pos = 0;

    while (!_should_stop) {
        if (cur_sampler_pos == 0) {

            if (_use_arm_timer) {
                start = _arm_timer.read();
            } else {
                start = read_cntvct_el0();
            }
        }

        std::tie(_sample_buf[cur_sampler_pos], now) = sample_once();
        _ts_buf[cur_sampler_pos] = (float)(now - start) * _timescale;

        cur_sampler_pos = (cur_sampler_pos + 1) % _sample_buf.size();
    }
}

void BufferedADC::start_sampling() {
    _sampler_thread = std::thread(&BufferedADC::worker, this);

    auto native_handle = _sampler_thread.native_handle();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(3, &cpuset);
    int result = pthread_setaffinity_np(native_handle, sizeof(cpu_set_t), &cpuset);
    if (result != 0) {
        throw std::runtime_error("ERROR SETTING THREAD AFFINITY");
    }

    sched_param sp;
    sp.sched_priority = sched_get_priority_max(SCHED_FIFO);;
    if (sp.sched_priority < 1) {
        throw std::runtime_error("ERROR GETTING MAX PRIORITY");
    }
    result = pthread_setschedparam(native_handle, SCHED_FIFO, &sp);
    if (result != 0) {
        throw std::runtime_error("ERROR SETTING THREAD PRIORITY");
    }
}

void BufferedADC::stop_sampling() {
    if (_worker_running) {
        _should_stop = true;
        _sampler_thread.join();
        _worker_running = false;
    }
}

std::tuple<float, uint64_t> BufferedADC::sample_once() {
    const char tx_buf[2] = {0x00, 0x00};
    char rx_buf[2];

    _spi.xfer(&tx_buf[0], &rx_buf[0], 2);

    uint64_t ts;
    if (_use_arm_timer) {
        ts = _arm_timer.read();
    } else {
        ts = read_cntvct_el0();
    }

    const float val = (
        _VDD * (float)(
            ((uint32_t)rx_buf[0] << 4) | ((uint32_t)rx_buf[1] >> 4)
        ) / 1024.f
    );
    return {val, ts};
}

std::tuple<std::vector<float>, std::vector<float>> BufferedADC::get_buffers() {
    if (!_worker_running) {
        throw std::runtime_error("Tried to get buffers before worker started.");
    }

    std::vector<float> sample_buf, ts_buf;

    std::tie(sample_buf, ts_buf) = {_sample_buf, _ts_buf};
    return {sample_buf, ts_buf};
}

void BufferedADC::set_use_arm_timer(bool use_arm_timer) {
    _use_arm_timer = use_arm_timer;

    if (_use_arm_timer) {
        _arm_timer.start();
        _timescale = 1.f / (float)ARM_TIMER_HZ;
    } else {
        _arm_timer.stop();
        _timescale = 1.f / (float)OSC_CLK_HZ;
    }
}
