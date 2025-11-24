#pragma once

#include <cstdint>

#define SPI_BASE_OFS 0x00204000
#define SPI_LEN 0x18

#define SPI_CS_OFS 0x00
#define SPI_FIFO_OFS 0x04
#define SPI_CDIV_OFS 0x08
#define SPI_DLEN_OFS 0x0c
#define SPI_DC_OFS 0x14

#define SPI0_GPIO_CE0 8
#define SPI0_GPIO_CE1 7
#define SPI0_GPIO_MISO 9
#define SPI0_GPIO_MOSI 10
#define SPI0_GPIO_SCLK 11

union SPIControlStatus {
    struct {
        uint32_t cs             : 2 = 0;
        uint32_t clk_pha        : 1 = 0;
        uint32_t clk_pol        : 1 = 0;
        uint32_t clear_tx       : 1 = 0;
        uint32_t clear_rx       : 1 = 0;
        uint32_t cs_pol         : 1 = 0;
        uint32_t xfer_active    : 1 = 0;
        uint32_t dma_enab       : 1 = 0;
        uint32_t int_on_done    : 1 = 0;
        uint32_t int_on_rxr     : 1 = 0;
        uint32_t adcs           : 1 = 0;
        uint32_t read_enab      : 1 = 0;
        uint32_t LoSSI_enab     : 1 = 0;
        uint32_t lmono_UNUSED   : 1 = 0;
        uint32_t te_en_UNUSED   : 1 = 0;
        uint32_t done           : 1 = 0;
        uint32_t readable       : 1 = 0;
        uint32_t writable       : 1 = 0;
        uint32_t needs_reading  : 1 = 0;
        uint32_t rx_fifo_full   : 1 = 0;
        uint32_t cs_pol_0       : 1 = 0;
        uint32_t cs_pol_1       : 1 = 0;
        uint32_t cs_pol_2       : 1 = 0;
        uint32_t dma_len        : 1 = 0;
        uint32_t len_long       : 1 = 0;
        uint32_t reserved       : 6 = 0;
    } flags;
    uint32_t bits;
};

union SPIDMAControl {
    struct {
        uint32_t tx_req_thresh      : 8 = 0;
        uint32_t tx_panic_thresh    : 8 = 0;
        uint32_t rx_req_thresh      : 8 = 0;
        uint32_t rx_panic_thresh    : 8 = 0;
    } flags;
    uint32_t bits;
};


/*
 * Helper for things like Python extensions so we don't have to wrap the whole
 * struct.
 */
inline int get_spi_flag_bits(uint32_t cs, uint32_t clk_pha, uint32_t clk_pol) {
    return SPIControlStatus{{.cs=cs, .clk_pha=clk_pha, .clk_pol=clk_pol}}.bits;
}
