add_library(spi OBJECT spi.cpp spi.hpp)
#target_include_directories(spi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET spi PROPERTY POSITION_INDEPENDENT_CODE ON)
