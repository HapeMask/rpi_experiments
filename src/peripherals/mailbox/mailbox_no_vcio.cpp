#include <iostream>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>

#include "peripherals/mailbox/mailbox_no_vcio.hpp"
#include "utils/reg_mem_utils.hpp"

Mailbox::Mailbox() {
    _virt_mbox_regs = map_phys_block((void*)(_asi.phys_mmio_base + MBOX_BASE_OFS), MBOX_LEN, _asi.page_size);

    _fifo_regs[0] = (volatile MboxMessageRef*)reg_addr(_virt_mbox_regs, MBOX0_FIFO_OFS);
    _fifo_regs[1] = (volatile MboxMessageRef*)reg_addr(_virt_mbox_regs, MBOX1_FIFO_OFS);
    _status_regs[0] = (volatile MboxStatus*)reg_addr(_virt_mbox_regs, MBOX0_STATUS_OFS);
    _status_regs[1] = (volatile MboxStatus*)reg_addr(_virt_mbox_regs, MBOX1_STATUS_OFS);
}

Mailbox::~Mailbox() {
    if (_virt_mbox_regs) {
        unmap_phys_block((void*)(_asi.phys_mmio_base + MBOX_BASE_OFS), MBOX_LEN, _asi.page_size);
    }
}
