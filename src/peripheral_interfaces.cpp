#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
using namespace pybind11::literals;
namespace py = pybind11;

#include "frequency_counter.hpp"
#include "peripherals/gpio/gpio.hpp"
#include "peripherals/clock/clock.hpp"
#include "peripherals/spi/spi_defs.hpp"


PYBIND11_MODULE(peripheral_interfaces, m, py::mod_gil_not_used()) {
    m.doc() = (
        "Peripheral interfaces: GPIO, FrequencyCounter, and SPI helpers."
    );

    m.def(
        "get_spi_flag_bits",
        &get_spi_flag_bits,
        "Get flag bits for given flags.",
        "cs"_a=0, "clk_pha"_a=0, "clk_pol"_a=0
    );

    py::class_<GPIO>(m, "GPIO")
        .def(py::init())
        .def("set_pin", &GPIO::set_pin, py::arg("pin"))
        .def("clear_pin", &GPIO::clear_pin, py::arg("pin"))
        .def("get_level", &GPIO::get_level, py::arg("pin"))
        .def("set_mode", &GPIO::set_mode, py::arg("pin"), py::arg("mode"));

    py::enum_<GPIOMode>(m, "GPIOMode")
        .value("IN", GPIOMode::IN)
        .value("OUT", GPIOMode::OUT)
        .value("ALT_0", GPIOMode::ALT_0)
        .value("ALT_1", GPIOMode::ALT_1)
        .value("ALT_2", GPIOMode::ALT_2)
        .value("ALT_3", GPIOMode::ALT_3)
        .value("ALT_4", GPIOMode::ALT_4)
        .value("ALT_5", GPIOMode::ALT_5)
        .export_values();

    py::class_<FrequencyCounter>(m, "FrequencyCounter")
        .def(
            py::init<int, int, int, int>(),
            py::arg("tgt_sample_rate")=50'000'000,
            py::arg("n_samples")=16384,
            py::arg("gpio_pin")=8,
            py::arg("dma_chan")=10
        )
        .def("sample", &FrequencyCounter::sample);
}
