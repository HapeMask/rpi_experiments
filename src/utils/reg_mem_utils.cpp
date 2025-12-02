#include <iostream>
#include <algorithm>
#include <cstring>
#include <bit>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sstream>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#include "utils/reg_mem_utils.hpp"

void* map_phys_block(const void* phys_addr, size_t size, size_t page_size) {
    int fd;
    void* virt;

    // mmap() requires page-aligned addresses for mapping. Compute the offset
    // within the page and remove it, then add it back when we return the
    // mapping.
    const auto ofs_within_page = ((uintptr_t)phys_addr) % page_size;
    const __off_t phys_addr_page = ((uintptr_t)phys_addr & ~(page_size - 1));

    if ((fd = open ("/dev/mem", O_RDWR|O_SYNC|O_CLOEXEC)) < 0) {
        throw std::runtime_error("Error: can't open /dev/mem, run using sudo.");
    }

    virt = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, phys_addr_page);
    close(fd);

    if (virt == MAP_FAILED) {
        std::ostringstream ss;
        ss << "Error: can't map memory. Message: ";
        ss << strerror(errno);
        throw std::runtime_error(ss.str());
    }

    return (void*)((uintptr_t)virt + ofs_within_page);
}

void unmap_phys_block(void* phys_addr, size_t size, size_t page_size) {
    if (phys_addr) {
        munmap((void*)((uintptr_t)phys_addr & ~(page_size - 1)), size);
    }
}

void* virt_to_phys(const void* virt_addr, uint32_t page_size) {
    uint64_t page_info;

    const auto ofs_within_page = ((uintptr_t)virt_addr) % page_size;
    const auto page_idx = (((uintptr_t)virt_addr) / page_size);
    const auto page_ofs = page_idx * sizeof(page_info);

    int fd = open("/proc/self/pagemap", 'r');
    if (lseek(fd, page_ofs, SEEK_SET) != static_cast<__off_t>(page_ofs)) {
        throw std::runtime_error("Failed to find entry in pagemap. Are you running as root?");
    }

    read(fd, &page_info, sizeof(page_info));
    close(fd);

    // PFN is bottom 55 bits (0-54) of the page info.
    const uint64_t PFN = page_info & (((uint64_t)1 << 55) - 1);
    return (void*)(PFN * page_size + ofs_within_page);
}

void* alloc_locked_block(size_t size, int page_size, bool zero) {
    void* mem = std::aligned_alloc(page_size, size);
    if (mlock(mem, size) == -1) {
        free(mem);
        throw std::runtime_error("Failed to lock memory block.");
    }

    if (zero) {
        std::fill((volatile char*)mem, (volatile char*)mem + size, 0);
    }
    return mem;
}

void clean_cache(const void* start, const void* end, int cache_line_size) {
    // TODO: What granularity is required here? Is clearing by cache line safe?
    char* start_aligned = (char*)((uintptr_t)start & ~(cache_line_size - 1));

    for(char* p=start_aligned; p < end; p += cache_line_size) {
        asm volatile (
            "dc civac, %0"
            : // No outputs.
            : "r"(p)
            : "memory"
        );
    }
}

static constexpr auto ranges_fn = "/proc/device-tree/soc/ranges";
static constexpr auto dma_ranges_fn = "/proc/device-tree/soc/dma-ranges";
void AddressSpaceInfo::read_device_tree_ranges() {
    const auto checked_open = [](const std::string& fn) {
        std::ifstream stream(fn);
        if (!stream.is_open()) {
            std::ostringstream ss;
            ss << "Failed to open device tree ranges file: " << fn;
            throw std::runtime_error(ss.str());
        }

        return stream;
    };

    const auto checked_read_32 = [](std::ifstream& ifs, const std::string& fn) {
        if (ifs.eof()) {
            std::ostringstream ss;
            ss << "Unexpected EOF while reading device tree ranges file: " << fn;
            throw std::runtime_error(ss.str());
        }

        // The device tree ranges file is specified to contain big-endian data,
        // but the system might be little endian. In that case, swap the byets.
        uint32_t val;
        ifs.read((char*)&val, sizeof(val));
        if (std::endian::native != std::endian::big) {
            val = std::byteswap(val);
        }
        return val;
    };

    auto ranges = checked_open(ranges_fn);
    auto dma_ranges = checked_open(dma_ranges_fn);

    bus_mmio_base = checked_read_32(ranges, ranges_fn);
    phys_mmio_base = checked_read_32(ranges, ranges_fn);
    mmio_size = checked_read_32(ranges, ranges_fn);

    bus_phys_mem_base = checked_read_32(dma_ranges, dma_ranges_fn);
    checked_read_32(dma_ranges, dma_ranges_fn); // Unused.
    bus_phys_mem_size = checked_read_32(dma_ranges, dma_ranges_fn);

    ranges.close();
    dma_ranges.close();
}
