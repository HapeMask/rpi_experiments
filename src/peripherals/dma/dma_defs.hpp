#pragma once

#include <cstdint>

static constexpr uint32_t DMA_BASE_OFS   = 0x00007000;
static constexpr uint32_t DMA_LEN        = 0xFF4;

static constexpr uint32_t DMA_ENABLE_OFS = 0xFF0;

inline constexpr uint32_t DMA_CHAN_OFS(int chan)     { return 0x100 * chan; }
inline constexpr uint32_t DMA_CS_OFS(int chan)       { return DMA_CHAN_OFS(chan) + 0x000; }
inline constexpr uint32_t DMA_CB_ADDR_OFS(int chan)  { return DMA_CHAN_OFS(chan) + 0x004; }
inline constexpr uint32_t DMA_TI_OFS(int chan)       { return DMA_CHAN_OFS(chan) + 0x008; }
inline constexpr uint32_t DMA_SRC_OFS(int chan)      { return DMA_CHAN_OFS(chan) + 0x00c; }
inline constexpr uint32_t DMA_DST_OFS(int chan)      { return DMA_CHAN_OFS(chan) + 0x010; }
inline constexpr uint32_t DMA_LEN_OFS(int chan)      { return DMA_CHAN_OFS(chan) + 0x014; }
inline constexpr uint32_t DMA_STRIDE_OFS(int chan)   { return DMA_CHAN_OFS(chan) + 0x018; }
inline constexpr uint32_t DMA_NEXT_CB_OFS(int chan)  { return DMA_CHAN_OFS(chan) + 0x01c; }
inline constexpr uint32_t DMA_DEBUG_OFS(int chan)    { return DMA_CHAN_OFS(chan) + 0x020; }

static constexpr uint32_t DMA_PERI_MAP_SMI       = 4;
static constexpr uint32_t DMA_PERI_MAP_PWM       = 5;
static constexpr uint32_t DMA_PERI_MAP_SPI_TX    = 6;
static constexpr uint32_t DMA_PERI_MAP_SPI_RX    = 7;

static constexpr uint32_t N_DMA_CHANS            = 15; // ignore the physically separate one

union DMAControlStatus {
    struct {
        uint32_t active             : 1 = 0;
        uint32_t end                : 1 = 0;
        uint32_t int_status         : 1 = 0;
        uint32_t dma_req            : 1 = 0;
        uint32_t paused             : 1 = 0;
        uint32_t paused_for_dma_req : 1 = 0;
        uint32_t waiting_on_writes  : 1 = 0;
        uint32_t resvd_0            : 1 = 0;
        uint32_t error              : 1 = 0;
        uint32_t resvd_1            : 7 = 0;
        uint32_t priority           : 4 = 0;
        uint32_t panic_priority     : 4 = 0;
        uint32_t resvd_3            : 4 = 0;
        uint32_t wait_for_writes    : 1 = 0;
        uint32_t ignore_debug_pause : 1 = 0;
        uint32_t abort              : 1 = 0;
        uint32_t reset              : 1 = 0;
    } flags;
    uint32_t bits;
};

union DMATransferInfo {
    struct {
        uint32_t int_enable         : 1 = 0;
        uint32_t _2d_mode           : 1 = 0;
        uint32_t resvd_0            : 1 = 0;
        uint32_t wait_for_writes    : 1 = 0;
        uint32_t dest_addr_incr     : 1 = 0;
        uint32_t dest_width_128     : 1 = 0;
        uint32_t dest_dma_req       : 1 = 0;
        uint32_t dest_ignore_writes : 1 = 0;
        uint32_t src_addr_incr      : 1 = 0;
        uint32_t src_width_128      : 1 = 0;
        uint32_t src_dma_req        : 1 = 0;
        uint32_t src_ignore_reads   : 1 = 0;
        uint32_t burst_len          : 4 = 0;
        uint32_t peri_map           : 5 = 0;
        uint32_t wait_cycles        : 5 = 0;
        uint32_t no_wide_bursts     : 1 = 0;
        uint32_t resvd_1            : 5 = 0;
    } flags;
    uint32_t bits;
};

struct alignas(32) DMAControlBlock {
    uint32_t ti;
    uint32_t src;
    uint32_t dst;
    uint32_t len;
    uint32_t stride = 0;
    uint32_t next_cb = 0;
    uint32_t debug = 0;
    uint32_t RESERVED_0 = 0;
};
