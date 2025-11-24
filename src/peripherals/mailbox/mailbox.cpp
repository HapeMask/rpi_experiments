#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>

#include "peripherals/mailbox/mailbox.hpp"
#include "peripherals/mailbox/mailbox_tag_defs.hpp"
#include "utils/reg_mem_utils.hpp"

Mailbox::Mailbox() {
    _vcio_fd = open("/dev/vcio", 0);
    if (_vcio_fd < 0) {
        throw std::runtime_error("Failed to open /dev/vcio.");
    }
}

Mailbox::~Mailbox() {
    if (_vcio_fd >= 0) {
        close(_vcio_fd);
    }
}

MemPtrs Mailbox::alloc_vc_mem(uint32_t size, uint32_t alignment) const {
    auto alloc_msg = MboxMessage<AllocMemPtrs>{
        .tags = {
            AllocMemPtrs {
                .req_size=size,
                .alignment=alignment,
                .alloc_flags={{.alias=MEM_ALIAS_DIRECT, .zero=1, .hint_permalock=1}},
            }
        }
    };
    xfer(alloc_msg);

    auto vc_mem_handle = std::get<0>(alloc_msg.tags).resp_handle;
    if (vc_mem_handle == 0) {
        throw std::runtime_error("Failed to allocate VC memory.");
    }

    auto lock_msg = MboxMessage<LockMemPtrs>{.tags={LockMemPtrs{.req_handle=vc_mem_handle}}};
    xfer(lock_msg);

    void* bus_addr = (void*)std::get<0>(lock_msg.tags).resp_bus_addr;
    if (bus_addr == 0) {
        throw std::runtime_error("Failed to lock VC memory.");
    }

    auto phys_addr = _asi.bus_to_phys(bus_addr);
    auto virt_addr = map_phys_block(phys_addr, size, _asi.page_size);

    return MemPtrs {
        .virt=virt_addr,
        .phys=phys_addr,
        .bus=bus_addr,
        .vc_handle=vc_mem_handle
    };
}

void Mailbox::free_vc_mem(MemPtrs mem) const {
    if (mem.vc_handle == 0) {
        return;
    }

    auto unlock_msg = MboxMessage<UnlockMemPtrs>{.tags={UnlockMemPtrs{.req_handle=mem.vc_handle}}};
    xfer(unlock_msg);
    if (std::get<0>(unlock_msg.tags).resp_status != 0) {
        throw std::runtime_error("Failed to unlock VC memory.");
    }

    auto release_msg = MboxMessage<ReleaseMemPtrs>{.tags={ReleaseMemPtrs{.req_handle=mem.vc_handle}}};
    xfer(release_msg);
    if (std::get<0>(release_msg.tags).resp_status != 0) {
        throw std::runtime_error("Failed to release VC memory.");
    }
}
