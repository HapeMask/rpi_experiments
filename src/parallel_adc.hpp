#pragma once
#include <vector>
#include <thread>
#include <semaphore>

#include <string>
#include <tuple>
#include <optional>
#include <utility>

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
namespace py = pybind11;

#include "peripherals/clock/clock.hpp"
#include "peripherals/dma/dma.hpp"
#include "peripherals/gpio/gpio.hpp"
#include "peripherals/pwm/pwm.hpp"
#include "peripherals/smi/smi.hpp"
#include "utils/reg_mem_utils.hpp"

#include "adc.hpp"

class ParallelADC : public ADC {
    public:
        ParallelADC(std::pair<float, float> vref, int n_samples=16384, int n_channels=2);
        virtual ~ParallelADC();

        uint32_t start_sampling(uint32_t sample_rate_hz) override;
        void toggle_channel(int channel_idx) override;
        void stop_sampling() override;
        void resize(int n_samples) override;

        int n_active_channels() const override;

        void set_logic_analyzer_mode(bool enable, int n_bits = 8) override;

    protected:
        uint32_t _cur_real_sample_rate = 0;
        int _n_channels = 0;

        void _fetch_data() override;
        float _sample_to_float(uint32_t raw_sample) const;

        std::vector<bool> _active_channels;
        int _highest_active_channel() const;

        void _setup_dma_cbs();

        MemPtrs _data;
        uint16_t* _rx_data_virt = nullptr;
        uint16_t* _rx_data_bus = nullptr;

        MemPtrs _la_data;
        uint32_t* _la_rx_data_virt = nullptr;
        uint32_t* _la_rx_data_bus = nullptr;

        DMA _dma;
        PWM _pwm{/*use_fifo=*/true};
        SMI _smi;
        GPIO _gpio;
        AddressSpaceInfo _asi;

        const int _dma_chan_0 = 9;
};
