#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
using namespace pybind11::literals;
namespace py = pybind11;

#include "serial_adc.hpp"
#include "parallel_adc.hpp"
#include "peripherals/gpio/gpio.hpp"
#include "peripherals/clock/clock.hpp"
#include "peripherals/spi/spi_defs.hpp"


PYBIND11_MODULE(ads7884, m, py::mod_gil_not_used()) {
    m.doc() = "Module to read from the ADS7884 ADC via SPI.";

    py::class_<SerialADC>(m, "SerialADC")
        .def(
            py::init<int, std::pair<float, float>, int>(),
            py::arg("spi_flag_bits"),
            py::arg("VREF")=std::make_pair(0.0f, 5.23f),
            py::arg("n_samples")=16384
        )
        .def_property("VREF", &SerialADC::VREF, nullptr)
        .def_property("n_samples", &SerialADC::n_samples, nullptr)
        .def("get_buffers", &SerialADC::get_buffers,
             py::arg("auto_range")=false,
             py::arg("low_thresh")=0.5f,
             py::arg("high_thresh")=2.5f,
             py::arg("trig_mode")="rising_edge",
             py::arg("skip_samples")=0
        )
        .def("start_sampling", &SerialADC::start_sampling)
        .def("stop_sampling", &SerialADC::stop_sampling)
        .def("toggle_channel", &SerialADC::toggle_channel)
        .def("n_active_channels", &SerialADC::n_active_channels)
        .def("resize", &SerialADC::resize);

    py::class_<ParallelADC>(m, "ParallelADC")
        .def(
            py::init<std::pair<float, float>, int, int>(),
            py::arg("VREF")=std::make_pair(0.f, 5.23f),
            py::arg("n_samples")=16384,
            py::arg("n_channels")=2
        )
        .def_property("VREF", &ParallelADC::VREF, nullptr)
        .def_property("n_samples", &ParallelADC::n_samples, nullptr)
        .def("get_buffers", &ParallelADC::get_buffers,
             py::arg("auto_range")=false,
             py::arg("low_thresh")=0.5f,
             py::arg("high_thresh")=2.5f,
             py::arg("trig_mode")="rising_edge",
             py::arg("skip_samples")=0
        )
        .def("start_sampling", &ParallelADC::start_sampling)
        .def("stop_sampling", &ParallelADC::stop_sampling)
        .def("toggle_channel", &ParallelADC::toggle_channel)
        .def("n_active_channels", &ParallelADC::n_active_channels)
        .def("resize", &ParallelADC::resize);

    m.def(
        "get_spi_flag_bits",
        &get_spi_flag_bits,
        "Get flag bits for given flags.",
        "cs"_a=0, "clk_pha"_a=0, "clk_pol"_a=0
    );

    py::class_<GPIO>(m, "GPIO")
        .def(
            py::init()
        )
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
}
