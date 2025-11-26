#pragma once

#include <cstdint>

// https://elinux.org/BCM2835_registers#SMI
// https://iosoft.blog/2020/07/16/raspberry-pi-smi/

static constexpr uint32_t SMI_BASE_OFS  = 0x00600000;
static constexpr uint32_t SMI_LEN  = 0x100;

static constexpr uint32_t SMI_CS_OFS           = 0x00;
static constexpr uint32_t SMI_LEN_OFS          = 0x04;
static constexpr uint32_t SMI_ADDR_OFS         = 0x08;
static constexpr uint32_t SMI_DATA_OFS         = 0x0c;
static constexpr uint32_t SMI_DMA_CTL_OFS      = 0x30;
static constexpr uint32_t SMI_DIRECT_CS_OFS    = 0x34;
static constexpr uint32_t SMI_DIRECT_ADDR_OFS  = 0x38;
static constexpr uint32_t SMI_DIRECT_DATA_OFS  = 0x3c;
static constexpr uint32_t SMI_FIFO_DBG_OFS     = 0x40;

static constexpr uint32_t N_SMI_DEVICE_CFGS    = 4;
inline constexpr uint32_t SMI_READ_CFG_OFS(int i)  { return 0x10 + 0x08 * i; }
inline constexpr uint32_t SMI_WRITE_CFG_OFS(int i) { return 0x14 + 0x08 * i; }

// Used in device config fields to set the parallel transfer width.
static constexpr uint32_t SMI_WIDTH_8_BITS  = 0;
static constexpr uint32_t SMI_WIDTH_16_BITS = 1;
static constexpr uint32_t SMI_WIDTH_18_BITS = 2;
static constexpr uint32_t SMI_WIDTH_9_BITS  = 3;

union SMIControlStatus {
    struct {
        uint32_t enable     : 1 = 0;
        uint32_t done       : 1 = 0;
        uint32_t active     : 1 = 0;
        uint32_t start      : 1 = 0;
        uint32_t clear      : 1 = 0;
        uint32_t write      : 1 = 0;
        uint32_t unused_1   : 2 = 0;
        uint32_t teen       : 1 = 0;
        uint32_t intd       : 1 = 0;
        uint32_t intt       : 1 = 0;
        uint32_t intr       : 1 = 0;
        uint32_t pvmode     : 1 = 0;
        uint32_t seterr     : 1 = 0;
        uint32_t pxldat     : 1 = 0;
        uint32_t edreq      : 1 = 0;
        uint32_t unused_2   : 8 = 0;
        uint32_t unused_3   : 1 = 0;
        uint32_t aferr      : 1 = 0;
        uint32_t txw        : 1 = 0;
        uint32_t rxr        : 1 = 0;
        uint32_t txd        : 1 = 0;
        uint32_t rxd        : 1 = 0;
        uint32_t txe        : 1 = 0;
        uint32_t rxf        : 1 = 0;
    } flags;
    uint32_t bits;
};

union SMIAddress {
    struct {
        uint32_t addr       : 6 = 0;
        uint32_t unused_1   : 2 = 0;
        uint32_t dev        : 2 = 0;
        uint32_t unused_2   : 22 = 0;
    } flags;
    uint32_t bits;
};

union SMIDMAControl {
    struct {
        uint32_t dma_req_write  : 6 = 0;
        uint32_t dma_req_read   : 6 = 0;
        uint32_t panic_write    : 6 = 0;
        uint32_t panic_read     : 6 = 0;
        uint32_t dmap           : 1 = 0;
        uint32_t unused_1       : 3 = 0;
        uint32_t dma_enable     : 1 = 0;
        uint32_t unused_2       : 3 = 0;
    } flags;
    uint32_t bits;
};

union SMIDeviceConfigRead {
    struct {
        uint32_t strobe	    : 7 = 0;
        uint32_t dma_req	: 1 = 0;
        uint32_t pace	    : 7 = 0;
        uint32_t paceall	: 1 = 0;
        uint32_t hold	    : 6 = 0;
        uint32_t fsetup	    : 1 = 0;
        uint32_t mode68	    : 1 = 0;
        uint32_t setup	    : 6 = 0;
        uint32_t width	    : 2 = 0;
    } flags;
    uint32_t bits;
};

union SMIDeviceConfigWrite {
    struct {
        uint32_t strobe	    : 7 = 0;
        uint32_t dma_req	: 1 = 0;
        uint32_t pace	    : 7 = 0;
        uint32_t paceall	: 1 = 0;
        uint32_t hold	    : 6 = 0;
        uint32_t swap	    : 1 = 0;
        uint32_t format	    : 1 = 0;
        uint32_t setup	    : 6 = 0;
        uint32_t width	    : 2 = 0;
    } flags;
    uint32_t bits;
};

union SMIDirectControlStatus {
    struct {
        uint32_t enable : 1 = 0;
        uint32_t start	: 1 = 0;
        uint32_t done	: 1 = 0;
        uint32_t write	: 1 = 0;
        uint32_t unused : 28 = 0;
    } flags;
    uint32_t bits;
};

union SMIFIFODebug {
    struct {
        uint32_t fifo_count : 6 = 0;
        uint32_t unused_0   : 2 = 0;
        uint32_t fifo_lvl	: 6 = 0;
        uint32_t unused_1   : 18 = 0;
    } flags;
    uint32_t bits;
};
