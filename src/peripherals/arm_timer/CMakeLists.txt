add_library(arm_timer OBJECT arm_timer.cpp arm_timer.hpp)
#target_include_directories(arm_timer PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET arm_timer PROPERTY POSITION_INDEPENDENT_CODE ON)
