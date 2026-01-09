cmake_minimum_required(VERSION 3.30)
project(RpiOsc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

find_package(pybind11 CONFIG REQUIRED)

add_compile_options(-g -O0 -Wall -Wextra -Wno-missing-field-initializers -fdebug-prefix-map=/home/debian/macos/rpi_experiments=..)
include_directories(src)

add_subdirectory(src/peripherals)
add_subdirectory(src/utils)

add_library(serial_adc STATIC src/serial_adc.cpp)
target_link_libraries(serial_adc clock dma gpio spi)
target_include_directories(serial_adc PRIVATE "${pybind11_INCLUDE_DIRS}")
set_target_properties(serial_adc PROPERTIES CXX_VISIBILITY_PRESET hidden)
set_target_properties(serial_adc PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(parallel_adc STATIC src/parallel_adc.cpp)
target_link_libraries(parallel_adc clock dma gpio smi)
target_include_directories(parallel_adc PRIVATE "${pybind11_INCLUDE_DIRS}")
set_target_properties(parallel_adc PROPERTIES CXX_VISIBILITY_PRESET hidden)
set_target_properties(parallel_adc PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_executable(gpio_pwm src/gpio_pwm.cpp)
target_link_libraries(gpio_pwm gpio pwm)

add_executable(gpclk_test src/gpclk_test.cpp)
target_link_libraries(gpclk_test clock gpio)

pybind11_add_module(adc_interfaces src/adc_interfaces.cpp)
target_link_libraries(adc_interfaces PRIVATE serial_adc parallel_adc)

# WHY ???
target_link_libraries(adc_interfaces PRIVATE -Wl,--whole-archive clock -Wl,--no-whole-archive)

add_custom_target(copy_py
    ALL # This ensures the target is built as part of the default build process
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/osc.py ${CMAKE_CURRENT_SOURCE_DIR}/custom_viewbox.py
            ${CMAKE_BINARY_DIR}/
    COMMENT "Copying Python code to build directory"
)
add_custom_target(copy_bash
    ALL # This ensures the target is built as part of the default build process
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/osc.bash
            ${CMAKE_BINARY_DIR}/osc.bash
    COMMENT "Copying Bash script to build directory"
)

add_custom_target(copy_resources
    ALL # This ensures the target is built as part of the default build process
    COMMAND ${CMAKE_COMMAND} -E copy_directory_if_different
            ${CMAKE_CURRENT_SOURCE_DIR}/resources
            ${CMAKE_BINARY_DIR}/resources
    COMMENT "Copying icon resources to build directory"
)
