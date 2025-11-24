#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>
#include <unistd.h>

#include "peripherals/dma/dma.hpp"
#include "peripherals/dma/dma_defs.hpp"
#include "utils/reg_mem_utils.hpp"

DMA::DMA(int n_cbs) : _max_cbs(0) {
    _virt_dma_regs = map_phys_block((void*)(_asi.phys_mmio_base + DMA_BASE_OFS), DMA_LEN, _asi.page_size);

    _enable_reg = reg_addr(_virt_dma_regs, DMA_ENABLE_OFS);

    for(int i=0; i < N_DMA_CHANS; ++i) {
        _cs_regs[i] = (volatile DMAControlStatus*)reg_addr(_virt_dma_regs, DMA_CS_OFS(i));
        _cb_addr_regs[i] = reg_addr(_virt_dma_regs, DMA_CB_ADDR_OFS(i));
        _ti_regs[i] = (volatile DMATransferInfo*)reg_addr(_virt_dma_regs, DMA_TI_OFS(i));
        _src_regs[i] = reg_addr(_virt_dma_regs, DMA_SRC_OFS(i));
        _dst_regs[i] = reg_addr(_virt_dma_regs, DMA_DST_OFS(i));
        _len_regs[i] = reg_addr(_virt_dma_regs, DMA_LEN_OFS(i));
        _debug_regs[i] = reg_addr(_virt_dma_regs, DMA_DEBUG_OFS(i));
    }

    resize_cbs(n_cbs);
}

DMA::~DMA() {
    if (_virt_dma_regs) {
        unmap_phys_block((void*)(_asi.phys_mmio_base + DMA_BASE_OFS), DMA_LEN, _asi.page_size);
    }

    if (_cb_mem.virt) {
        if (_use_vc_mem) {
            _mbox.free_vc_mem(_cb_mem);
            _cb_mem.virt = nullptr;
            _cb_mem.phys = nullptr;
            _cb_mem.bus = nullptr;
            _cb_mem.vc_handle = 0;
        } else {
            free(_cb_mem.virt);
        }
    }
}

void DMA::resize_cbs(int new_size) {
    _n_cbs = new_size;

    if (_n_cbs == 0 || _n_cbs > _max_cbs) {
        if(_cb_mem.virt) {
            if (_use_vc_mem) {
                _mbox.free_vc_mem(_cb_mem);
            } else {
                free(_cb_mem.virt);
            }
        }
    }

    if (_n_cbs == 0) {
        _max_cbs = 0;
    }

    if (_n_cbs > _max_cbs) {
        const auto bytes_to_alloc = _n_cbs * sizeof(DMAControlBlock);
        if (_use_vc_mem) {
            _cb_mem = _mbox.alloc_vc_mem(bytes_to_alloc, _asi.page_size);
        } else {
            _cb_mem.virt = alloc_locked_block(
                bytes_to_alloc,
                _asi.page_size,
                /*zero=*/true
            );
            _cb_mem.phys = virt_to_phys(_cb_mem.virt, _asi.page_size);
            _cb_mem.bus = _asi.phys_to_bus(_cb_mem.phys);
        }

        _max_cbs = _n_cbs;
    }
}

DMAControlBlock& DMA::get_cb(size_t i) {
    if (i >= _n_cbs) {
        throw std::runtime_error("Index out of range.");
    }
    return ((DMAControlBlock*)_cb_mem.virt)[i];
}

const DMAControlBlock& DMA::get_cb(size_t i) const {
    if (i >= _n_cbs) {
        throw std::runtime_error("Index out of range.");
    }
    return ((DMAControlBlock*)_cb_mem.virt)[i];
}

DMAControlBlock* DMA::get_cb_bus_ptr(size_t i) {
    if (i >= _n_cbs) {
        throw std::runtime_error("Index out of range.");
    }
    return (DMAControlBlock*)_cb_mem.bus + i;
}

const DMAControlBlock* DMA::get_cb_bus_ptr(size_t i) const {
    if (i >= _n_cbs) {
        throw std::runtime_error("Index out of range.");
    }
    return (DMAControlBlock*)_cb_mem.bus + i;
}

void DMA::show_active_dma_chans() const {
    for (int i=0; i < N_DMA_CHANS; ++i) {
        const bool global_enable = (*_enable_reg) & (1 << i);
        const bool enabled = global_enable && ((*_src_regs[i] != 0) || (*_dst_regs[i] != 0));
        if (enabled) {
            std::cout << "DMA channel " << i << " enabled." << std::endl;
        }
    }
}

void DMA::reset(int channel) const {
    _cs_regs[channel]->flags.reset = 1;
}

void DMA::enable(int channel) const {
    *_enable_reg |= (1 << channel);
}

void DMA::disable(int channel) const {
    *_enable_reg &= ~(1 << channel);
}

bool DMA::error(int channel) const {
    return _cs_regs[channel]->flags.error;
}

void DMA::start(int channel, int first_cb_idx) const {
    if (!_use_vc_mem) {
        clean_cache(_cb_mem.virt, (DMAControlBlock*)_cb_mem.virt + _n_cbs, _asi.cache_line_size);
    }

    enable(channel);
    reset(channel);

    *_cb_addr_regs[channel] = (uint32_t)(uintptr_t)get_cb_bus_ptr(first_cb_idx);
    _cs_regs[channel]->flags.end = 1;
    *_debug_regs[channel] = 7;
    _cs_regs[channel]->flags.active = 1;
}

void DMA::wait(int channel, int max_retries, int delay_us) const {
    for(int i=0; i < max_retries; ++i) {
        if (_cs_regs[channel]->flags.active == 0 && (*_len_regs[channel]) == 0) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
    }

    std::ostringstream ss;
    ss << "DMA::wait() timed out. Bytes remaining: ";
    ss << (*_len_regs[channel]);
    throw std::runtime_error(ss.str());
}
