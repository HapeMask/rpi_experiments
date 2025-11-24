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

#include "peripherals/gpio/gpio.hpp"
#include "utils/rpi_zero_2.hpp"

static bool running = true;

void shutdown(int signum) {
    running = false;
}

void csleep(float time) {
    const auto ticks = time * 19.2e6;
    const auto start = read_cntvct_el0();
    while (read_cntvct_el0() < (start + ticks)) {
        asm volatile("yield");
    }
}

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "Usage: ./gpio_osc PIN DUTY_CYCLE[0,1] FREQUENCY(Hz)" << std::endl;
        return 1;
    }

    const int pin = std::stoi(argv[1]);
    const float duty_cycle = std::stof(argv[2]);
    const float freq = std::stof(argv[3]);
    const float period = 1.f / freq;

    signal(SIGINT, shutdown);

    GPIO gpio;

    gpio.set_mode(pin, GPIOMode::OUT);

    while (running) {
        gpio.set_pin(pin);
        csleep(duty_cycle * period);
        gpio.clear_pin(pin);
        csleep((1.f - duty_cycle) * period);
    }

    std::cout << "Stopping..." << std::endl;

    return 0;
}
