#pragma once

#include "utils/reg_mem_utils.hpp"

class Peripheral {
    public:
        Peripheral(uint32_t reg_base_ofs, uint32_t reg_len);
        virtual ~Peripheral();

        inline void* reg_to_bus(uint32_t reg_ofs_bytes) const {
            return (void*)(_asi.bus_mmio_base + _reg_base_ofs + reg_ofs_bytes);
        }

        inline volatile uint32_t* reg_addr(uint32_t ofs_bytes) const {
            return (volatile uint32_t*)((uint8_t*)_virt_regs_ptr + ofs_bytes);
        }

    protected:
        uint32_t _reg_base_ofs = 0;
        uint32_t _reg_len = 0;
        volatile void* _virt_regs_ptr = nullptr;

        AddressSpaceInfo _asi;
};
