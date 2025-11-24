#include <iostream>
#include <chrono>
#include <thread>

#include "dma_adc.hpp"

constexpr int spd_hz = 16000000;
constexpr float VDD = 5.23;

int main(int argc, char** argv) {
    int n_samples = 32767;
    int spi_spd_hz = 16000000;
    DMAADC adc(spi_spd_hz, get_spi_flag_bits(0, 1, 0), VDD, n_samples);
    adc.start_sampling();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::vector<float> sample_buf, ts_buf;
    std::tie(sample_buf, ts_buf) = adc.get_buffers();

    for(int i=0; i<sample_buf.size(); ++i) {
        std::cout << ts_buf[i] << " " << sample_buf[i] << std::endl;
    }
    return 0;
}
