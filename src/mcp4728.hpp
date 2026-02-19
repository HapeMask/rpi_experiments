#pragma once

#include <vector>
#include <string>
#include <tuple>
#include <optional>
#include <cstdint>

class MCP4728 {
public:
    /**
     * @brief Interface for the MCP4728 quad 12-bit DAC connected via I2C.
     * 
     * @param Vdd The supply voltage, used for external reference calculation.
     * @param address The I2C address of the device (default 0x60).
     * @param i2c_device The I2C device path (default "/dev/i2c-1").
     */
    MCP4728(
        double Vdd,
        uint8_t address = 0x60,
        const std::string& i2c_device = "/dev/i2c-1",
        bool auto_ref = true
    );
    ~MCP4728();

    /**
     * @brief Sets the voltages for all channels.
     * 
     * If a voltage for a channel is not provided, that channel keeps its current setting.
     * 
     * @param v0 Voltage for channel 0 (optional).
     * @param v1 Voltage for channel 1 (optional).
     * @param v2 Voltage for channel 2 (optional).
     * @param v3 Voltage for channel 3 (optional).
     */
    void set_voltages(
        std::optional<double> v0 = std::nullopt,
        std::optional<double> v1 = std::nullopt,
        std::optional<double> v2 = std::nullopt,
        std::optional<double> v3 = std::nullopt
    );

    inline void set_auto_ref(bool auto_ref) { _auto_ref = auto_ref; }
    inline void toggle_auto_ref() { _auto_ref = !_auto_ref; }

private:
    std::tuple<int, int, double> _gain_vref_settings(double v);
    std::vector<uint8_t> _get_update_bytes(int channel, double v);

    double _Vdd;
    uint8_t _address;
    int _i2c_fd;

    double _max_voltage_internal;
    double _max_voltage_external;

    std::vector<double> _cur_voltages;
    bool _auto_ref;

    static constexpr int DAC_BITS = 12;
    static constexpr uint16_t MAX_CODE = (1 << DAC_BITS) - 1;
    static constexpr double INTERNAL_VREF = 2.048;
};
