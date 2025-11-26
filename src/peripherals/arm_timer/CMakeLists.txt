add_library(arm_timer STATIC arm_timer.cpp arm_timer.hpp)
target_link_libraries(arm_timer peripheral)
set_property(TARGET arm_timer PROPERTY POSITION_INDEPENDENT_CODE ON)
