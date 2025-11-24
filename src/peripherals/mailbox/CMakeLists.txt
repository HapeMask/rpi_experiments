add_library(mailbox OBJECT mailbox.cpp mailbox.hpp)
#target_include_directories(mailbox PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_property(TARGET mailbox PROPERTY POSITION_INDEPENDENT_CODE ON)
