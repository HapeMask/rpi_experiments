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
            py::init<int, int, std::pair<float, float>, int>(),
            py::arg("spi_speed"),
            py::arg("spi_flag_bits"),
            py::arg("VREF")=std::make_pair(0.0f, 5.23f),
            py::arg("n_samples")=16384
        )
        .def_property("VREF", &SerialADC::VREF, nullptr)
        .def_property("n_samples", &SerialADC::n_samples, nullptr)
        .def("get_buffers", &SerialADC::get_buffers)
        .def("start_sampling", &SerialADC::start_sampling)
        .def("stop_sampling", &SerialADC::stop_sampling);

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
             py::arg("auto_trig")=false,
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
}
