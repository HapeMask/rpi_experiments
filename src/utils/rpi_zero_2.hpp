#pragma once

static constexpr int OSC_CLK_HZ   = 19200000;
static constexpr int ARM_TIMER_HZ = 250000000;
static constexpr int PLLD_CLK_HZ  = 500000000;
static constexpr int SPI_CLOCK_HZ = 250000000;

/*
 * Initializes the performance counters to enable the cycle counter. Requires
 * that you first run:
       asm volatile("msr pmuserenr_el0, %0" :: "r" (1 << 0));
   from kernel space (i.e. a custom kernel module).
*/
inline void init_pmccntr() {
    uintptr_t cr = 0;
    asm volatile("mrs %0, pmcr_el0" : "=r"(cr));
    cr |= (1 << 0); // Enable counters
    cr |= (1 << 2); // Clear counters
    cr &= ~(1 << 3); // Bit 3 divides count by 64, clear it to disable
    asm volatile("msr pmcr_el0, %0" :: "r"(cr));

    // Bit 31: Enable cycle counter
    // Bits 0-3: Enable performance counters 0-3
    asm volatile("mrs %0, pmcntenset_el0" : "=r"(cr));
    cr |= (1 << 31);
    asm volatile("msr pmcntenset_el0, %0" :: "r"(cr));
    asm volatile("isb");
}

/*
 * Read the current CPU cycle count from the performance counter.
*/
inline uint64_t read_pmccntr_el0() {
    uintptr_t pmccntr_el0 = 0;
    asm volatile("mrs %0, pmccntr_el0" : "=r"(pmccntr_el0));
    return pmccntr_el0;
}

/*
 * Read the current virtual cycle count from the (slower) counter.
*/
inline uint64_t read_cntvct_el0() {
    uintptr_t cntvct_el0;
    asm volatile("mrs %0, cntvct_el0" : "=r"(cntvct_el0));
    return cntvct_el0;
}
