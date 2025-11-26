add_library(gpio STATIC gpio.cpp gpio.hpp)
target_link_libraries(gpio peripheral)
set_property(TARGET gpio PROPERTY POSITION_INDEPENDENT_CODE ON)
