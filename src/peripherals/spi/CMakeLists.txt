add_library(spi STATIC spi.cpp spi.hpp)
target_link_libraries(spi peripheral)
set_property(TARGET spi PROPERTY POSITION_INDEPENDENT_CODE ON)
