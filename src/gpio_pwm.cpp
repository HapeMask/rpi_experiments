#include <iostream>
#include <cstdio>
#include <cmath>
#include <thread>
#include <chrono>
#include <string>

#include <signal.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "peripherals/pwm/pwm.hpp"
#include "peripherals/gpio/gpio.hpp"

static PWM* pwm_ptr = nullptr;

void shutdown(int signum) {
    if (pwm_ptr) {
        pwm_ptr->stop();
    }
    exit(signum);
}

void validate_inputs(int pin, float duty_cycle, float freq, bool use_m_s, ClockSource clk_src) {
    if (duty_cycle > 1 or duty_cycle < 0) {
        std::cerr << "Duty cycle must be a floating point value between 0 and 1." << std::endl;
        exit(1);
    }

    float min_freq, max_freq;
    if (use_m_s) {
        min_freq = 0.f;
        max_freq = CLOCK_HZ[clk_src];
    } else {
        // TODO;
        min_freq = 0;
        max_freq = 0;
    }

    if (freq < min_freq or freq > max_freq) {
        std::cerr << "Frequency must be within [" << min_freq << ", " << max_freq << "]" <<std::endl;
        exit(1);
    }

    if (pin != 12 && pin != 18) {
        std::cerr << "Only pins 12 and 18 can produce PWM signals." << std::endl;
        exit(1);
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: ./gpio_pwm PIN{12,18} DUTY_CYCLE[0,1] FREQUENCY(Hz)" << std::endl;
        return 1;
    }

    const ClockSource clk_src = ClockSource::PLLD;
    const int pin = std::stoi(argv[1]);
    const float duty_cycle = std::stof(argv[2]);
    const float freq = std::stof(argv[3]);
    const bool use_m_s = true;

    validate_inputs(pin, duty_cycle, freq, use_m_s, clk_src);

    signal(SIGINT, shutdown);

    GPIO gpio;
    PWM pwm(/*use_fifo=*/false);
    pwm_ptr = &pwm;

    gpio.set_mode(pin, (pin == 18) ? GPIOMode::ALT_5 : GPIOMode::ALT_0);

    pwm.setup_clock(duty_cycle, freq, clk_src);
    pwm.start();

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    shutdown(0);
    return 0;
}
