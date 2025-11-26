cmake_minimum_required(VERSION 3.30)
project(RpiOsc LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION OFF)

find_package(pybind11 CONFIG REQUIRED)

add_compile_options(-g -O0 -Wall -Wextra -fdebug-prefix-map=/home/debian/macos/rpi_experiments=..)
include_directories(src)

add_subdirectory(src/peripherals)
add_subdirectory(src/utils)

add_library(buffered_adc STATIC src/buffered_adc.cpp)
target_link_libraries(buffered_adc PRIVATE dma)
set_property(TARGET buffered_adc PROPERTY POSITION_INDEPENDENT_CODE ON)

add_library(dma_adc STATIC src/dma_adc.cpp)
target_link_libraries(dma_adc PRIVATE dma)
set_property(TARGET dma_adc PROPERTY POSITION_INDEPENDENT_CODE ON)

add_executable(dma_test src/dma_test.cpp)
target_link_libraries(dma_test PRIVATE dma spi gpio pwm)

add_executable(dma_adc_test src/dma_adc_test.cpp)
target_link_libraries(dma_adc_test PRIVATE dma dma_adc spi gpio pwm)

add_executable(gpio_osc src/gpio_osc.cpp)
target_link_libraries(gpio_osc PRIVATE gpio)

add_executable(gpio_pwm src/gpio_pwm.cpp)
target_link_libraries(gpio_pwm PRIVATE gpio pwm)

pybind11_add_module(ads7884 src/ads7884.cpp)
target_link_libraries(ads7884 PRIVATE arm_timer buffered_adc dma dma_adc gpio spi)

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
