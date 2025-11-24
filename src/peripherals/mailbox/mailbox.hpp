#pragma once

// <3 https://bitbanged.com/posts/understanding-rpi/the-mailbox/ <3

#include <stdexcept>
#include <sys/ioctl.h>

#include "utils/reg_mem_utils.hpp"

#define MBOX_BASE_OFS 0x0000B880
#define MBOX_LEN 0x20

#define MBOX0_FIFO_OFS 0x00
#define MBOX0_STATUS_OFS 0x18
#define MBOX1_FIFO_OFS 0x20
#define MBOX1_STATUS_OFS 0x38

#define MBOX_STATUS_SUCCESS 0x80000000
#define MBOX_STATUS_FAILURE 0x80000001

union MboxMessageRef {
    struct {
        uint32_t channel    : 4 = 0;
        uint32_t data       : 28 = 0;
    } flags;
    uint32_t bits;
};

union MboxStatus {
    struct {
        uint32_t level      : 7 = 0;
        uint32_t reserved   : 23 = 0;
        uint32_t empty      : 1 = 0;
        uint32_t full       : 1 = 0;
    } flags;
    uint32_t bits;
};

union MboxTagStatus {
    struct {
        uint32_t resp_buf_sz    : 31 = 0;
        uint32_t is_resp        : 1 = 0;
    } flags;
    uint32_t bits;
};

union MboxConfig {
    struct {
        uint32_t irq_space              : 1 = 0;
        uint32_t irq_new_mail           : 1 = 0;
        uint32_t irq_opp_empty          : 1 = 0;
        uint32_t clear                  : 1 = 0;
        uint32_t enable_irq_space       : 1 = 0;
        uint32_t enable_irq_new_mail    : 1 = 0;
        uint32_t enable_irq_opp_empty   : 1 = 0;
        uint32_t err_non_owner_read     : 1 = 0;
        uint32_t err_write_to_full      : 1 = 0;
        uint32_t err_read_from_empty    : 1 = 0;
    } flags;
    uint32_t bits;
};

template <typename... TagType>
struct alignas(16) MboxMessage {
    // Total buffer length (bytes)
    const uint32_t len = 3 * sizeof(uint32_t) + sizeof(std::tuple<TagType...>);

    // 0 for request, other values reserved.
    // 0x80000000 -> Successful response.
    // 0x80000001 -> Unsuccessful response.
    /*volatile*/ uint32_t status = 0;

    /*volatile*/ std::tuple<TagType...> tags;

    const uint32_t end_tag = 0;
};

class Mailbox {
    public:
        Mailbox();
        virtual ~Mailbox();

        template <typename... TagType>
        void xfer(MboxMessage<TagType...>& msg, uint32_t channel=8) const;

        MemPtrs alloc_vc_mem(uint32_t size, uint32_t alignment=4096) const;
        void free_vc_mem(MemPtrs mem) const;

    protected:
        template <typename... TagType>
        void send(MboxMessage<TagType...>* msg, uint32_t channel) const;

        int _vcio_fd = -1;
        AddressSpaceInfo _asi;
};

template <typename... TagType>
void Mailbox::send(MboxMessage<TagType...>* msg, uint32_t channel) const {
    if (channel != 8) {
        throw std::runtime_error("Only channel 8 is supported for now.");
    }

    if ((uintptr_t)msg & 0xf) {
        throw std::runtime_error("Unaligned message pointer.");
    }

    if (ioctl(_vcio_fd, _IOWR(100, 0, void*), msg) < 0) {
        throw std::runtime_error("VC mailbox ioctl() failed.");
    }
}

template <typename... TagType>
void Mailbox::xfer(MboxMessage<TagType...>& msg, uint32_t channel) const {
    send(&msg, channel);

    if (msg.status != MBOX_STATUS_SUCCESS) {
        throw std::runtime_error("Received bad response. Ill-formed request.");
    }
}
