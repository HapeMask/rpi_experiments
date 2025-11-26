#pragma once

#include "peripherals/peripheral.hpp"
#include "peripherals/smi/smi_defs.hpp"
#include "utils/reg_mem_utils.hpp"

class SMI : public Peripheral {
    public:
        SMI();
        virtual ~SMI();

        void* reg_to_bus(uint32_t reg_ofs_bytes) const;

    protected:
        volatile SMIControlStatus* _cs_reg = nullptr;
        volatile uint32_t* _len_reg = nullptr;
        volatile SMIAddress* _addr_reg = nullptr;
        volatile uint32_t* _data_reg = nullptr;
        volatile SMIDeviceConfigRead* _read_cfg_regs[N_SMI_DEVICE_CFGS];
        volatile SMIDeviceConfigWrite* _write_cfg_regs[N_SMI_DEVICE_CFGS];
        volatile SMIDMAControl* _dma_ctl_reg = nullptr;
        volatile SMIDirectControlStatus* _direct_cs_reg = nullptr;
        volatile SMIAddress* _direct_addr_reg = nullptr;
        volatile uint32_t* _direct_data_reg = nullptr;
};
