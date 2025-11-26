add_library(smi STATIC smi.cpp smi.hpp)
target_link_libraries(smi peripheral)
set_property(TARGET smi PROPERTY POSITION_INDEPENDENT_CODE ON)
