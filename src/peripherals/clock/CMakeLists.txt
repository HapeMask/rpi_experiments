add_library(clock STATIC clock.cpp clock.hpp)
target_link_libraries(clock peripheral)
set_property(TARGET clock PROPERTY POSITION_INDEPENDENT_CODE ON)
