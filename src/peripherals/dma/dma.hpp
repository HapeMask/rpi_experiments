#include <vector>

#include "peripherals/dma/dma_defs.hpp"
#include "peripherals/mailbox/mailbox.hpp"
#include "utils/reg_mem_utils.hpp"

struct alignas(32) DMAControlBlock {
    uint32_t ti;
    uint32_t src;
    uint32_t dst;
    uint32_t len;
    uint32_t stride = 0;
    uint32_t next_cb = 0;
    uint32_t debug = 0;
    uint32_t RESERVED_0 = 0;
};

class DMA {
    public:
        DMA(int n_cbs=0);
        virtual ~DMA();

        void show_active_dma_chans() const;

        void reset(int channel) const;
        void enable(int channel) const;
        void disable(int channel) const;
        bool error(int channel) const;
        void start(int channel, int first_cb_idx) const;
        void wait(int channel, int max_retries=1000, int delay_us=100) const;

        DMAControlBlock& get_cb(size_t i);
        const DMAControlBlock& get_cb(size_t i) const;

        DMAControlBlock* get_cb_bus_ptr(size_t i);
        const DMAControlBlock* get_cb_bus_ptr(size_t i) const;

        volatile uint32_t* _dst_regs[N_DMA_CHANS];
        volatile uint32_t* _len_regs[N_DMA_CHANS];

        bool _use_vc_mem = true;
        Mailbox _mbox;

    //protected:
        size_t _n_cbs = 0;
        size_t _max_cbs = 0;
        MemPtrs _cb_mem;

        void* _virt_dma_regs = nullptr;
        volatile uint32_t* _enable_reg = nullptr;
        volatile DMAControlStatus* _cs_regs[N_DMA_CHANS];
        volatile uint32_t* _cb_addr_regs[N_DMA_CHANS];
        volatile DMATransferInfo* _ti_regs[N_DMA_CHANS];
        volatile uint32_t* _src_regs[N_DMA_CHANS];
        volatile uint32_t* _debug_regs[N_DMA_CHANS];

        void resize_cbs(int new_size);

        AddressSpaceInfo _asi;
};
