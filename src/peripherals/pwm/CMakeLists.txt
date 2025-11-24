add_library(pwm OBJECT pwm.cpp pwm.hpp)
#target_include_directories(pwm PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET pwm PROPERTY POSITION_INDEPENDENT_CODE ON)
