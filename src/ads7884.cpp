#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
using namespace pybind11::literals;
namespace py = pybind11;

#include "buffered_adc.hpp"
#include "dma_adc.hpp"
#include "peripherals/spi/spi_defs.hpp"

PYBIND11_MODULE(ads7884, m, py::mod_gil_not_used()) {
    m.doc() = "Module to read from the ADS7884 ADC via SPI.";

    py::class_<BufferedADC>(m, "BufferedADC")
        .def(
            py::init<int, int, float, int>(),
            py::arg("spi_speed"),
            py::arg("spi_flag_bits"),
            py::arg("VDD"),
            py::arg("n_samples")=16384
        )
        .def_property("VDD", &BufferedADC::VDD, nullptr)
        .def_property("use_arm_timer", &BufferedADC::get_use_arm_timer, &BufferedADC::set_use_arm_timer)
        .def("get_buffers", &BufferedADC::get_buffers)
        .def("start_sampling", &BufferedADC::start_sampling)
        .def("stop_sampling", &BufferedADC::stop_sampling);

    py::class_<DMAADC>(m, "DMAADC")
        .def(
            py::init<int, int, float, int>(),
            py::arg("spi_speed"),
            py::arg("spi_flag_bits"),
            py::arg("VDD"),
            py::arg("n_samples")=16384
        )
        .def_property("VDD", &DMAADC::VDD, nullptr)
        .def("get_buffers", &DMAADC::get_buffers)
        .def("start_sampling", &DMAADC::start_sampling)
        .def("stop_sampling", &DMAADC::stop_sampling);

    m.def(
        "get_spi_flag_bits",
        &get_spi_flag_bits,
        "Get flag bits for given flags.",
        "cs"_a=0, "clk_pha"_a=0, "clk_pol"_a=0
    );
}
