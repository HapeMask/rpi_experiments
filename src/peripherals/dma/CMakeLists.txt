add_library(dma OBJECT dma.cpp dma.hpp)
#target_include_directories(dma PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET dma PROPERTY POSITION_INDEPENDENT_CODE ON)
