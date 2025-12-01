#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
using namespace pybind11::literals;
namespace py = pybind11;

#include "serial_adc.hpp"
#include "parallel_adc.hpp"
#include "peripherals/clock/clock.hpp"
#include "peripherals/spi/spi_defs.hpp"


PYBIND11_MODULE(ads7884, m, py::mod_gil_not_used()) {
    m.doc() = "Module to read from the ADS7884 ADC via SPI.";

    py::class_<SerialADC>(m, "SerialADC")
        .def(
            py::init<int, int, float, int>(),
            py::arg("spi_speed"),
            py::arg("spi_flag_bits"),
            py::arg("VDD"),
            py::arg("n_samples")=16384
        )
        .def_property("VDD", &SerialADC::VDD, nullptr)
        .def_property("n_samples", &SerialADC::n_samples, nullptr)
        .def("get_buffers", &SerialADC::get_buffers)
        .def("start_sampling", &SerialADC::start_sampling)
        .def("stop_sampling", &SerialADC::stop_sampling);

    py::class_<ParallelADC>(m, "ParallelADC")
        .def(
            py::init<float, int>(),
            py::arg("VDD"),
            py::arg("n_samples")=16384
        )
        .def_property("VDD", &ParallelADC::VDD, nullptr)
        .def_property("n_samples", &ParallelADC::n_samples, nullptr)
        .def("get_buffers", &ParallelADC::get_buffers)
        .def("start_sampling", &ParallelADC::start_sampling)
        .def("stop_sampling", &ParallelADC::stop_sampling)
        .def("toggle_channel", &ParallelADC::toggle_channel)
        .def("n_active_channels", &ParallelADC::n_active_channels);

    m.def(
        "get_spi_flag_bits",
        &get_spi_flag_bits,
        "Get flag bits for given flags.",
        "cs"_a=0, "clk_pha"_a=0, "clk_pol"_a=0
    );
}
