#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

#include <signal.h>
#include <unistd.h>

#include "peripherals/dma/dma.hpp"
#include "peripherals/mailbox/mailbox.hpp"
#include "peripherals/pwm/pwm.hpp"
#include "peripherals/pwm/pwm_defs.hpp"
#include "peripherals/spi/spi.hpp"
//#include "peripherals/spi/spi_defs.hpp"
#include "utils/reg_mem_utils.hpp"
#include "utils/rpi_zero_2.hpp"


constexpr int rx_block_size = 32768;

int main(int argc, char** argv) {
    const AddressSpaceInfo asi;
    Mailbox mbox;

    // Double the PWM frequency because each PWM pulse triggers a DMA, and we
    // need 2 DMAs to turn on+off.
    float duty_cycle = 0.5;
    float freq = 16000 * 2;
    //PWM pwm(duty_cycle, freq, /*use_fifo=*/true);

    //pin_data = 1 << (pin % 32);
    //pwm_data = pwm.get_data();

    const int n_samples = 16384;
    // Receive the data using one CB for each 1024-byte block. IDK.
    const int n_rx_cbs = ((2 * n_samples) + (rx_block_size - 1)) / rx_block_size;

    DMA dma(/*n_cbs=*/2 + n_rx_cbs);
    SPI spi(32000000,  {{.clk_pha=1}});

    const int n_locked_bytes = (n_samples * sizeof(uint16_t) + 3 * sizeof(uint32_t));
    bool use_vc_mem = true;
    MemPtrs src_data;

    if (use_vc_mem) {
        src_data = mbox.alloc_vc_mem(
            n_locked_bytes,
            asi.page_size
        );
    } else {
        src_data.virt = alloc_locked_block(n_locked_bytes, asi.page_size);
        src_data.phys = virt_to_phys(src_data.virt, asi.page_size);
        src_data.bus = asi.phys_to_bus(src_data.phys);
    }


    //uint32_t& pin_data = src_data[0];
    //uint32_t& pwm_data = src_data[1];
    uint32_t* tx_data_virt = (uint32_t*)src_data.virt;
    uint32_t* tx_data_bus = (uint32_t*)src_data.bus;
    uint8_t* rx_data_virt = (uint8_t*)(tx_data_virt + 3);
    uint8_t* rx_data_bus = (uint8_t*)(tx_data_bus + 3);

    auto& cb0 = dma.get_cb(0);
    auto& cb1 = dma.get_cb(1);

    auto spi_fifo_bus_addr = (uint32_t)(uintptr_t)spi.reg_to_bus(SPI_FIFO_OFS);

    // Send SPI setup word and initial CS high bits, then switch to loop on CB1 toggling CS.
    cb0.ti = DMATransferInfo{{.wait_for_writes=1, .dest_dma_req=1, .src_addr_incr=1, .peri_map=DMA_PERI_MAP_SPI_TX}}.bits;
    cb0.src = (uint32_t)(uintptr_t)tx_data_bus;
    cb0.dst = spi_fifo_bus_addr;
    cb0.len = 4 + 4;
    cb0.next_cb = (uint32_t)(uintptr_t)dma.get_cb_bus_ptr(1);

    tx_data_virt[0] = (
        (2 * n_samples) << 16
        | (SPIControlStatus{{.clk_pha=1, .xfer_active=1}}.bits & 0xff)
    );
    tx_data_virt[1] = 0b11111111111111111111111111111111;


    // Toggle CS in a loop. 
    cb1.ti = DMATransferInfo{{.wait_for_writes=1, .dest_dma_req=1, .src_addr_incr=0, .peri_map=DMA_PERI_MAP_SPI_TX}}.bits;
    cb1.src = (uint32_t)(uintptr_t)(tx_data_bus + 2);
    cb1.dst = spi_fifo_bus_addr;
    cb1.len = 4;
    cb1.next_cb = (uint32_t)(uintptr_t)dma.get_cb_bus_ptr(1);

    tx_data_virt[2] = 0b00000001000000000000000100000000;

    uint8_t* cur_rx_ptr = rx_data_bus;
    int rx_bytes_rem = 2 * n_samples;
    for(int i=2; i<n_rx_cbs + 2; ++i) {
        auto& cbi = dma.get_cb(i);
        cbi.ti = DMATransferInfo{{.wait_for_writes=1, .dest_addr_incr=1, .src_dma_req=1, .peri_map=DMA_PERI_MAP_SPI_RX}}.bits;
        cbi.src = spi_fifo_bus_addr;
        cbi.dst = (uint32_t)(uintptr_t)cur_rx_ptr;
        cbi.len = std::min(rx_block_size, rx_bytes_rem);

        if (i < (n_rx_cbs + 2 - 1)) {
            cbi.next_cb = (uint32_t)(uintptr_t)dma.get_cb_bus_ptr(i + 1);
        } else {
            cbi.next_cb = 0;
        }

        cur_rx_ptr += rx_block_size;
        rx_bytes_rem -= rx_block_size;
    }

    /*
    cb0.ti = cb1.ti = cb2.ti = cb3.ti = (uint32_t)(DMATransferInfo::DST_DREQ | DMATransferInfo::PERI_MAP_PWM);
    cb0.len = cb1.len = cb2.len = cb3.len = 4;

    // Set the GPIO pin.
    cb0.src = (uint32_t)(uintptr_t)asi.virt_to_bus(&pin_data);
    cb0.dst = (uint32_t)(uintptr_t)gpio.reg_to_bus(GPIO_OUT_SET_OFS);

    // Write to PWM FIFO to clear DMA request.
    cb1.src = (uint32_t)(uintptr_t)asi.virt_to_bus(&pwm_data);
    cb1.dst = (uint32_t)(uintptr_t)pwm.reg_to_bus(PWM0_FIF_OFS);

    // Clear the GPIO pin.
    cb2.src = (uint32_t)(uintptr_t)asi.virt_to_bus(&pin_data);
    cb2.dst = (uint32_t)(uintptr_t)gpio.reg_to_bus(GPIO_OUT_CLR_OFS);

    // Write to PWM FIFO to clear DMA request.
    cb3.src = (uint32_t)(uintptr_t)asi.virt_to_bus(&pwm_data);
    cb3.dst = (uint32_t)(uintptr_t)pwm.reg_to_bus(PWM0_FIF_OFS);

    cb0.next_cb = (uint32_t)(uintptr_t)asi.virt_to_bus(&cb1);
    cb1.next_cb = (uint32_t)(uintptr_t)asi.virt_to_bus(&cb2);
    cb2.next_cb = (uint32_t)(uintptr_t)asi.virt_to_bus(&cb3);
    cb3.next_cb = (uint32_t)(uintptr_t)asi.virt_to_bus(&cb0);
    */

    //const int dma_chan = 10;
    const int dma_chan_0 = 9;
    const int dma_chan_1 = 10;

    //std::cerr << "cb2 dst pre: " << ((void*)cb2.dst) << std::endl;
    //std::cerr << "dst reg pre: " << ((void*)(*dma._dst_regs[dma_chan_1])) << std::endl;
    //std::cerr << "len reg pre: " << (*dma._len_regs[dma_chan_1]) << std::endl;

    if (!use_vc_mem) {
        clean_cache((void*)src_data.virt, ((char*)src_data.virt) + n_locked_bytes, asi.cache_line_size);
    }

    const auto start = read_cntvct_el0();
    spi.start_dma(4, 8, 4, 8);
    dma.start(dma_chan_0, /*first_cb_idx=*/0);
    dma.start(dma_chan_1, /*first_cb_idx=*/2);
    dma.wait(dma_chan_1);
    const auto end = read_cntvct_el0();

    if (!use_vc_mem) {
        clean_cache((void*)src_data.virt, ((char*)src_data.virt) + n_locked_bytes, asi.cache_line_size);
    }

    std::cerr << (1e-6 * n_samples / ((end - start) / 19.2e6)) << " MS/s" << std::endl;

    uint32_t post_len = (*dma._len_regs[dma_chan_1]);

    if (post_len > 0) {
        std::cerr << "len reg post: " << post_len << std::endl;
    }

    // add alignment check for CB ptrs, bottom 5 bits, assert !(cb_addr & 31)

    //pwm.enable_dma(/*dreq_thresh=*/1);
    //std::this_thread::sleep_for(std::chrono::microseconds(100));

    //pwm.start();
    //std::this_thread::sleep_for(std::chrono::microseconds(100));
    //dma.start(dma_chan, cb0);

    for(int i=2; i<n_samples; ++i) {
        std::cout << (
            5.23f * (float)(
                ((uint32_t)rx_data_virt[2 * i + 0] << 4) | ((uint32_t)rx_data_virt[2 * i + 1] >> 4)
            ) / 1024.f
        ) << std::endl;
    }

    //pwm.stop();
    //dma.reset(dma_chan);
    dma.reset(dma_chan_0);
    dma.reset(dma_chan_1);

    //dma.disable(dma_chan);
    dma.disable(dma_chan_0);
    dma.disable(dma_chan_1);
    spi.stop_dma();

    if (use_vc_mem) {
        mbox.free_vc_mem(src_data);
    } else {
        free((void*)src_data.virt);
    }

    return 0;
}
