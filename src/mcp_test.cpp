#include "mcp4728.hpp"
#include <iostream>
#include <chrono>
#include <cmath>
#include <thread>
#include <csignal>
#include <atomic>

std::atomic<bool> running(true);

void signal_handler(int signal) {
    if (signal == SIGINT) {
        running = false;
    }
}

int main(int argc, char** argv) {
    double Vdd = 5.25;
    if (argc > 1) {
        Vdd = std::stod(argv[1]);
    }

    try {
        MCP4728 dac(Vdd);
        std::signal(SIGINT, signal_handler);

        double f = 800.0;
        double A = 1.0;
        
        if (A > (Vdd / 2.0)) {
            std::cerr << "Amplitude too high for Vdd: " << Vdd << std::endl;
            return 1;
        }

        std::cout << "Starting sine wave on channel 0 (freq=" << f << "Hz, amp=" << A << "V)" << std::endl;
        std::cout << "Press Ctrl+C to stop." << std::endl;

        auto start = std::chrono::high_resolution_clock::now();
        while (running) {
            auto now = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double> elapsed = now - start;
            double t = elapsed.count();
            
            double v = (Vdd / 2.0) + A * std::sin(2.0 * M_PI * t * f);
            
            dac.set_voltages(v, 0.0, 0.0, 0.0);
            
            // To prevent saturating CPU we could add a tiny sleep, 
            // but for frequency response we want it fast.
            // std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        std::cout << "Stopping and setting all channels to 0V." << std::endl;
        dac.set_voltages(0.0, 0.0, 0.0, 0.0);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
