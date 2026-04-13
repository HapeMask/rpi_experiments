#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
using namespace pybind11::literals;
namespace py = pybind11;

#include "adc.hpp"
#include "parallel_adc.hpp"
#include "serial_adc.hpp"


PYBIND11_MODULE(adc_interfaces, m, py::mod_gil_not_used()) {
    m.doc() = (
        "Module exposing ADC classes (SerialADC, ParallelADC)."
    );

    py::enum_<TrigMode>(m, "TrigMode")
        .value("NONE", TrigMode::NONE)
        .value("RISING_EDGE", TrigMode::RISING_EDGE)
        .value("FALLING_EDGE", TrigMode::FALLING_EDGE)
        .export_values();

    py::class_<ADC>(m, "ADC")
        .def("get_buffers", &ADC::get_buffers,
             py::arg("screen_width"),
             py::arg("x_range")=std::make_pair(0.0, -1.0),
             py::arg("auto_range")=false,
             py::arg("thresh")=std::make_pair(0.5f, 2.5f),
             py::arg("trig_mode")=TrigMode::RISING_EDGE,
             py::arg("skip_samples")=0
        )
        .def_property("VREF", &ADC::VREF, nullptr)
        .def("start_sampling", &ADC::start_sampling)
        .def("stop_sampling", &ADC::stop_sampling)
        .def("toggle_channel", &ADC::toggle_channel)
        .def("n_active_channels", &ADC::n_active_channels)
        .def("channel_active", &ADC::channel_active)
        .def("resize", &ADC::resize)
        .def("set_logic_analyzer_mode", &ADC::set_logic_analyzer_mode,
             py::arg("enable"),
             py::arg("n_bits")=8
        )
        .def_property_readonly("logic_analyzer_mode", &ADC::logic_analyzer_mode)
        .def_property_readonly("data_generation", &ADC::data_generation)
        .def_property_readonly("n_samples", &ADC::n_samples)
        .def_property_readonly("n_channels", &ADC::n_channels);

    py::class_<SerialADC, ADC>(m, "SerialADC")
        .def(
            py::init<int, std::pair<float, float>, int, int>(),
            py::arg("spi_flag_bits"),
            py::arg("VREF")=std::make_pair(0.0f, 5.23f),
            py::arg("n_samples")=16384,
            py::arg("n_channels")=1
        );

    py::class_<ParallelADC, ADC>(m, "ParallelADC")
        .def(
            py::init<std::pair<float, float>, int, int, int>(),
            py::arg("VREF")=std::make_pair(0.f, 5.23f),
            py::arg("n_samples")=16384,
            py::arg("n_channels")=2,
            // TODO: Make all ADCs support different number formats.
            // TODO: Make an enum for this too. 
            // 0: offset binary
            // 1: 2's complement
            py::arg("bit_format")=1
        );
}
