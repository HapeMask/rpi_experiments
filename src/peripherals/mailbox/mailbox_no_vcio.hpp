#pragma once

// <3 https://bitbanged.com/posts/understanding-rpi/the-mailbox/ <3

#include <stdexcept>
#include <sys/ioctl.h>

#include "utils/reg_mem_utils.hpp"

static constexpr uint32_t MBOX_BASE_OFS  = 0x0000B880;
static constexpr uint32_t MBOX_LEN       = 0x20;

static constexpr uint32_t MBOX0_FIFO_OFS     = 0x00;
static constexpr uint32_t MBOX0_STATUS_OFS   = 0x18;
static constexpr uint32_t MBOX1_FIFO_OFS     = 0x20;
static constexpr uint32_t MBOX1_STATUS_OFS   = 0x38;

static constexpr uint32_t MBOX_STATUS_SUCCESS = 0x80000000;
static constexpr uint32_t MBOX_STATUS_FAILURE = 0x80000001;

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
    /*volatile*/ uint32_t status;

    /*volatile*/ std::tuple<TagType...> tags;

    const uint32_t end_tag = 0;
};

class Mailbox {
    public:
        Mailbox();
        virtual ~Mailbox();

        template <typename... TagType>
        void xfer(MboxMessage<TagType...>& msg, uint32_t channel=8);

    protected:
        template <typename... TagType>
        void send(MboxMessage<TagType...>* msg, uint32_t channel);
        MboxMessageRef recv(uint32_t channel=8);

        void* _virt_mbox_regs = nullptr;
        volatile MboxMessageRef* _fifo_regs[2] = {nullptr, nullptr};
        volatile MboxStatus* _status_regs[2] = {nullptr, nullptr};

        AddressSpaceInfo _asi;
};

template <typename... TagType>
void Mailbox::send(MboxMessage<TagType...>* msg, uint32_t channel) {
    if ((uintptr_t)msg & 0xf) {
        throw std::runtime_error("Unaligned message pointer.");
    }

    while (_status_regs[1]->flags.full) {
        asm volatile("yield");
    }

    _fifo_regs[1]->bits = MboxMessageRef{{.channel=channel, .data=((uint32_t)(uintptr_t)msg) >> 4}}.bits;
}

MboxMessageRef Mailbox::recv(uint32_t channel) {
    MboxMessageRef mref {.bits=0};
    MboxStatus status {.bits=0};

    while (_status_regs[0]->flags.empty) {
        asm volatile("yield");
    }

    do {
        mref = MboxMessageRef{.bits=_fifo_regs[0]->bits};
    } while (mref.flags.channel != channel);
    return mref;
}

template <typename... TagType>
void Mailbox::xfer(MboxMessage<TagType...>& msg, uint32_t channel) {
    auto msg_ptr_bus = (MboxMessage<TagType...>*)_asi.virt_to_bus(&msg);

    clean_cache((void*)&msg, (void*)((char*)&msg + msg.len), _asi.cache_line_size);

    send(msg_ptr_bus, channel);
    const auto mref = recv();

    // TODO: check mref and stuff.
    while(!(msg.status & 0x80000000)) {
        clean_cache((void*)&msg, (void*)((char*)&msg + msg.len), _asi.cache_line_size);
    }

    if (msg.status != MBOX_STATUS_SUCCESS) {
        throw std::runtime_error("Received bad response. Ill-formed request.");
    }
}
