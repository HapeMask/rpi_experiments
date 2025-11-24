#pragma once

#include <iostream>
#include <cstdint>
#include <cstddef>
#include <tuple>
#include <unistd.h>

struct AddressSpaceInfo;

inline volatile uint32_t* reg_addr(void* base, uint32_t ofs_bytes) {
    return (volatile uint32_t*)((uint8_t*)base + ofs_bytes);
}

void* map_phys_block(const void* phys_addr, size_t size, size_t page_size);
void unmap_phys_block(void* phys_addr, size_t size, size_t page_size);
void* virt_to_phys(const void* virt_addr, uint32_t page_size);
void* alloc_locked_block(size_t size, int page_size, bool zero=true);
void clean_cache(const void* start, const void* end, int cache_line_size);

class AddressSpaceInfo {
    public:
        AddressSpaceInfo() :
            page_size(sysconf(_SC_PAGESIZE)),
            cache_line_size(sysconf(_SC_LEVEL1_DCACHE_LINESIZE))
        {
            read_device_tree_ranges();
        }

        // Use uintptr_t to avoid lots of casting later. Requires that we use more
        // storage on 64-bit OSs and some extra work to read ranges files.
        uintptr_t bus_mmio_base;        // Bus address for MMIO (peripherals etc...).
        uintptr_t phys_mmio_base;       // Physical address for MMIO (peripherals etc...).
        uintptr_t mmio_size;            // Size of the MMIO segment.

        uintptr_t bus_phys_mem_base;    // Bus address corresponding to physical address 0.
        uintptr_t bus_phys_mem_size;    // Size of the physical memory segment mapped to bus addresses.

        uint32_t page_size;
        uint32_t cache_line_size;

        inline void* bus_to_phys(const void* addr) const {
            return (void*)((uintptr_t)addr & ~bus_phys_mem_base);
        }

        inline void* phys_to_bus(const void* addr) const {
            return (void*)((uintptr_t)addr | bus_phys_mem_base);
        }

        inline void* virt_to_bus(const void* addr) const {
            return phys_to_bus(virt_to_phys(addr, page_size));
        }

    protected:
        void read_device_tree_ranges();
};

struct MemPtrs {
    void* virt = nullptr;
    void* phys = nullptr;
    void* bus = nullptr;

    uint32_t vc_handle = 0;
};
