#include <chrono>
#include <iostream>
#include <thread>
#include <unistd.h>

#include "peripherals/mailbox/mailbox.hpp"
#include "peripherals/mailbox/mailbox_utils.hpp"
#include "utils/reg_mem_utils.hpp"

int main(int argc, char** argv) {
    Mailbox mbox;

    auto msg = MboxMessage<GetDisplaySize, GetFramebufferSize, GetTemperature>();
    mbox.xfer(msg);

    std::cout << "Display: " << std::get<0>(msg.tags).width << "x" << std::get<0>(msg.tags).height << std::endl;
    std::cout << "Framebuffer: " << std::get<1>(msg.tags).width << "x" << std::get<1>(msg.tags).height << std::endl;
    std::cout << "Temperature: " << (std::get<2>(msg.tags).temp_thousandths_deg_c / 1000.f) << std::endl;

    return 0;
}
