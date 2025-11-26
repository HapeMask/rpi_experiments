add_library(dma STATIC dma.cpp dma.hpp)
target_link_libraries(dma peripheral mailbox)
set_property(TARGET dma PROPERTY POSITION_INDEPENDENT_CODE ON)
