#pragma once

#include "peripherals/mailbox/mailbox.hpp"

static constexpr int MEM_ALIAS_NORMAL           = 0; // normal allocating alias. Don't use from ARM
static constexpr int MEM_ALIAS_DIRECT           = 1; // 0xC alias uncached
static constexpr int MEM_ALIAS_COHERENT         = 2; // 0x8 alias. Non-allocating in L2 but coherent
static constexpr int MEM_ALIAS_L1_NONALLOCATING = 3; // Allocating in L2
union MboxAllocFlags {
    struct {
        uint32_t discardable    : 1 = 0; // Can be resized to 0 at any time. Use for cached data.
        uint32_t unused_0       : 1 = 0;
        uint32_t alias          : 2 = 0; // See aliases above.
        uint32_t zero           : 1 = 0; // Initialise buffer to all zeros.
        uint32_t no_init        : 1 = 0; // Don't initialise (default is initialise to all ones).
        uint32_t hint_permalock : 1 = 0; // Likely to be locked for long periods of time.
    } flags;
    uint32_t bits;
};

template <uint32_t ID, size_t VSZ>
struct MboxTag {
    static_assert((VSZ & (4 - 1)) == 0, "VSZ must be a multiple of 4 bytes.");

    const uint32_t id = ID;
    const uint32_t val_buf_sz = VSZ;

    MboxTagStatus status = {.bits=0};
};

struct AllocMemPtrs : MboxTag<0x0003000C, 12> {
    union {
        uint32_t req_size = 0;
        uint32_t resp_handle;
    };
    uint32_t alignment = 0;
    MboxAllocFlags alloc_flags {.bits=0};
};

struct LockMemPtrs : MboxTag<0x0003000D, 4> {
    union {
        uint32_t req_handle = 0;
        uint32_t resp_bus_addr;
    };
};

struct UnlockMemPtrs : MboxTag<0x0003000E, 4> {
    union {
        uint32_t req_handle = 0;
        uint32_t resp_status;
    };
};

struct ReleaseMemPtrs : MboxTag<0x0003000F, 4> {
    union {
        uint32_t req_handle = 0;
        uint32_t resp_status;
    };
};

struct GetDisplaySize : MboxTag<0x00040003, 8> {
    uint32_t width = 0;
    uint32_t height = 0;
};

struct GetFramebufferSize : MboxTag<0x00040004, 8> {
    uint32_t width = 0;
    uint32_t height = 0;
};

struct GetTemperature : MboxTag<0x00030006, 8> {
    const uint32_t temp_id = 0;
    uint32_t temp_thousandths_deg_c = 0;
};
