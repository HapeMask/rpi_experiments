#include <iostream>
#include <chrono>
#include <thread>
#include <string>

#include <signal.h>

#include "peripherals/clock/clock.hpp"
#include "peripherals/gpio/gpio.hpp"

static bool done = false;

void shutdown([[maybe_unused]] int signum) {
    done = true;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: ./gpclk_test SRC FREQ" << std::endl;
        return 1;
    }

    const std::string src_str = std::string(argv[1]);
    const int clk_hz = std::stof(argv[2]);

    if (src_str != "PLLD" && src_str != "PLLC" && src_str != "OSC") {
        std::cerr << "SRC must be one of {PLLD, PLLC, OSC}." << std::endl;
        return 1;
    }

    const auto src = (
        (src_str == "PLLD") ?  ClockSource::PLLD :
        ((src_str == "PLLC") ? ClockSource::PLLC : ClockSource::OSC)
    );

    signal(SIGINT, shutdown);

    GPIO gpio;
    Clock clock;

    gpio.set_mode(4, GPIOMode::ALT_0);

    const auto actual_hz = clock.start_clock(ClockID::GP0, src, clk_hz);
    std::cout << "Actual clock frequency: " << actual_hz << std::endl;

    while (!done) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << "Done." << std::endl;

    return 0;
}
