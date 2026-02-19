#include "mcp4728.hpp"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <unistd.h>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <iostream>

MCP4728::MCP4728(double Vdd, uint8_t address, const std::string& i2c_device, bool auto_ref)
    : _Vdd(Vdd), _address(address), _cur_voltages(4, -1.0), _auto_ref(auto_ref) {
    // _cur_voltages is initialized to -1.0 to force first update.

    _i2c_fd = open(i2c_device.c_str(), O_RDWR);
    if (_i2c_fd < 0) {
        throw std::runtime_error("Failed to open I2C device: " + i2c_device);
    }

    if (ioctl(_i2c_fd, I2C_SLAVE, _address) < 0) {
        close(_i2c_fd);
        throw std::runtime_error("Failed to acquire bus access and/or talk to MCP4728.");
    }

    _max_voltage_internal = INTERNAL_VREF * (static_cast<double>(MAX_CODE) / (1 << DAC_BITS));
    _max_voltage_external = Vdd * (static_cast<double>(MAX_CODE) / (1 << DAC_BITS));

    // Initialize all channels to 0V
    set_voltages(0.0, 0.0, 0.0, 0.0);
}

MCP4728::~MCP4728() {
    if (_i2c_fd >= 0) {
        close(_i2c_fd);
    }
}

std::tuple<int, int, double> MCP4728::_gain_vref_settings(double v) {
    if (v > std::max(2.0 * _max_voltage_internal, _max_voltage_external)) {
        throw std::invalid_argument("Voltage " + std::to_string(v) + " exceeds maximum possible DAC output");
    }

    int vref = 0;
    int gain = 0;
    double max_v = _max_voltage_external;

    if (_auto_ref && v <= 2.0 * _max_voltage_internal) {
        vref = 1;
        max_v = _max_voltage_internal;

        if (v > _max_voltage_internal) {
            gain = 1;
            max_v *= 2.0;
        }
    }

    return {gain, vref, max_v};
}

std::vector<uint8_t> MCP4728::_get_update_bytes(int channel, double v) {
    const auto [gain_bit, vref_bit, max_v] = _gain_vref_settings(v);
    uint16_t v_int = static_cast<uint16_t>(MAX_CODE * v / max_v);
    if (v_int > MAX_CODE) {
        v_int = MAX_CODE;
    }

    // Command: 0100 0[DAC1][DAC0][UDAC_inv]
    //
    // Setting UDAC_inv and then later UDAC to 0 in byte_0 is fine because
    // subsequent byte_0s seem to be ignored in the multi-write command? The
    // first byte_0 has UDAC_inv = 0 which triggers the write.
    const uint8_t byte_0 = 0b01000000 | (channel << 1);
    const uint8_t byte_1 = (vref_bit << 7) | (gain_bit << 4) | (v_int >> 8);
    const uint8_t byte_2 = (v_int & 0xFF);

    return {byte_0, byte_1, byte_2};
}

void MCP4728::set_voltages(
    std::optional<double> v0,
    std::optional<double> v1,
    std::optional<double> v2,
    std::optional<double> v3
) {
    std::vector<std::optional<double>> inputs = {v0, v1, v2, v3};
    std::vector<double> tgt_voltages = _cur_voltages;
    std::vector<std::pair<int, double>> to_update;

    for (int i = 0; i < 4; ++i) {
        if (inputs[i].has_value()) {
            tgt_voltages[i] = inputs[i].value();
        }
        if (tgt_voltages[i] != _cur_voltages[i]) {
            to_update.push_back({i, tgt_voltages[i]});
        }
    }

    if (to_update.empty()) {
        return;
    }

    std::vector<uint8_t> data;
    for(const auto& [c, v] : to_update) {
        for(const auto& b : _get_update_bytes(c, v)) {
            data.push_back(b);
        }
    }

    if (write(_i2c_fd, data.data(), data.size()) != static_cast<ssize_t>(data.size())) {
        throw std::runtime_error("I2C write failed.");
    }

    _cur_voltages = tgt_voltages;
}
