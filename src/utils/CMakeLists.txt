add_library(reg_mem_utils OBJECT reg_mem_utils.cpp)
#target_include_directories(reg_mem_utils PUBLIC src/peripherals src/utils)
set_property(TARGET reg_mem_utils PROPERTY POSITION_INDEPENDENT_CODE ON)
