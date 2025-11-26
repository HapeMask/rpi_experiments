#include "peripherals/smi/smi.hpp"
#include "peripherals/smi/smi_defs.hpp"

SMI::SMI() : Peripheral(SMI_BASE_OFS, SMI_LEN) {
    _cs_reg = (volatile SMIControlStatus*)reg_addr(SMI_CS_OFS);
    _len_reg = reg_addr(SMI_LEN_OFS);
    _addr_reg = (volatile SMIAddress*) reg_addr(SMI_ADDR_OFS);
    _data_reg = reg_addr(SMI_DATA_OFS);

    for(int i=0; i<N_SMI_DEVICE_CFGS; ++i) {
        _read_cfg_regs[i] = (
            (volatile SMIDeviceConfigRead*)
            reg_addr(SMI_READ_CFG_OFS(i))
        );
        _write_cfg_regs[i] = (
            (volatile SMIDeviceConfigWrite*)
            reg_addr(SMI_WRITE_CFG_OFS(i))
        );
    }

    _dma_ctl_reg = (volatile SMIDMAControl*)reg_addr(SMI_DMA_CTL_OFS);
    _direct_cs_reg = (volatile SMIDirectControlStatus*)reg_addr(SMI_DIRECT_CS_OFS);
    _direct_addr_reg = (volatile SMIAddress*)reg_addr(SMI_DIRECT_ADDR_OFS);
    _direct_data_reg = reg_addr(SMI_DIRECT_DATA_OFS);
}
