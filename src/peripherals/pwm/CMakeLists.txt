add_library(pwm STATIC pwm.cpp pwm.hpp)
target_link_libraries(pwm peripheral clock)
set_property(TARGET pwm PROPERTY POSITION_INDEPENDENT_CODE ON)
