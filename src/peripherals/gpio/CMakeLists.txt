add_library(gpio OBJECT gpio.cpp gpio.hpp)
#target_include_directories(gpio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET gpio PROPERTY POSITION_INDEPENDENT_CODE ON)
