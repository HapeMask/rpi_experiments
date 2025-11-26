#include "peripherals/peripheral.hpp"

Peripheral::Peripheral(uint32_t reg_base_ofs, uint32_t reg_len) :
    _reg_base_ofs(reg_base_ofs),
    _reg_len(reg_len)
{
    _virt_regs_ptr = map_phys_block(
        (void*)(_asi.phys_mmio_base + _reg_base_ofs), _reg_len, _asi.page_size
    );
}

Peripheral::~Peripheral() {
    if (_virt_regs_ptr) {
        unmap_phys_block(
            (void*)(_asi.phys_mmio_base + _reg_base_ofs), _reg_len, _asi.page_size
        );
        _virt_regs_ptr = nullptr;
    }
}
