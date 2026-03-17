#include <chrono>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <tuple>

#include <signal.h>

#include "frequency_counter.hpp"


static bool done = false;

void shutdown([[maybe_unused]] int signum) {
    done = true;
}

std::tuple<std::string, std::string, float> get_si_prefixes(float val) {
    std::string pref = "";
    std::string inv_pref = "";
    float scale = 1;

    if (val == 0) {
        return {pref, inv_pref, scale};
    }

    val = std::abs(val);
    bool flip = false;

    if (val < 1) {
        flip = true;
        val = 1.f / val;
    }

    auto lval = std::log10(val);

    if (lval >= 9) {
        pref = "G";
        inv_pref = "n";
        scale = 1e9;
    } else if (lval >= 6) {
        pref = "M";
        inv_pref = "u";
        scale = 1e6;
    } else if (lval >= 3) {
        pref = "K";
        inv_pref = "m";
        scale = 1e3;
    }

    if (flip) {
        std::swap(pref, inv_pref);
    }

    return {pref, inv_pref, scale};
}

int main(int argc, char** argv) {
    FrequencyCounter fcount;

    signal(SIGINT, shutdown);
    while (!done) {
        const float freq = fcount.sample();
        const auto [pref, inv_pref, scale] = get_si_prefixes(freq);

        std::cout << "\rFreq: " << (freq / scale) << " " << pref << "Hz    " << std::flush;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    std::cout << std::endl;

    return 0;
}
