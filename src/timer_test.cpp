#include <iostream>
#include <chrono>
#include <thread>

#include "peripherals/arm_timer/arm_timer.hpp"

int main(int argc, char** argv) {
    ARMTimer timer;
    timer.start();
    constexpr int N = 100000;

    const auto startc = std::chrono::high_resolution_clock::now();
    const auto start = timer.read();

    for(int i=0; i<N; ++i) {
        asm volatile("nop");
    }

    const auto end = timer.read();
    const auto endc = std::chrono::high_resolution_clock::now();

    const auto ticks = end - start;
    const auto elapsed = std::chrono::duration<float>(endc - startc).count();

    std::cout << "ns / tick: " << (1e9 * (elapsed / ticks)) << std::endl;
    return 0;
}
