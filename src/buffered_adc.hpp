#pragma once
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>

#include "peripherals/arm_timer/arm_timer.hpp"
#include "peripherals/gpio/gpio.hpp"
#include "peripherals/spi/spi.hpp"
#include "peripherals/spi/spi_defs.hpp"
#include "utils/rpi_zero_2.hpp"
#include "utils/reg_mem_utils.hpp"

class BufferedADC {
    public:
        BufferedADC(int spi_speed, uint32_t spi_flag_bits, float vdd, int n_samples=16384);
        virtual ~BufferedADC();

        void start_sampling();
        void stop_sampling();
        void resize(int N);

        std::tuple<std::vector<float>, std::vector<float>> get_buffers();
        void set_use_arm_timer(bool use_arm_timer);
        bool get_use_arm_timer() const { return _use_arm_timer; }
        float VDD() const { return _VDD; }

    protected:
        void worker();
        std::tuple<float, uint64_t> sample_once();
        bool _use_arm_timer;
        float _timescale;

        bool _worker_running = false;
        bool _should_stop = false;
        std::thread _sampler_thread;

        ARMTimer _arm_timer;
        GPIO _gpio;
        SPI _spi;

        std::vector<float> _sample_buf;
        std::vector<float> _ts_buf;
        float _VDD;
};
