#pragma once

#include "peripherals/gpio/gpio_defs.hpp"
#include "utils/reg_mem_utils.hpp"

class GPIO {
    public:
        GPIO();
        virtual ~GPIO();

        volatile uint32_t* get_mode_reg(int pin) const;
        volatile uint32_t* get_set_reg(int pin) const;
        volatile uint32_t* get_clr_reg(int pin) const;
        volatile uint32_t* get_lvl_reg(int pin) const;

        void set_pin(int pin) const;
        void clear_pin(int pin) const;
        bool get_level(int pin) const;
        void set_mode(int pin, GPIOMode mode) const;
        void* reg_to_bus(uint32_t reg_ofs_bytes) const;

    protected:
        void* _virt_gpio_regs = nullptr;
        volatile uint32_t* _mode_regs[N_GPIO_MODE_REGS];
        volatile uint32_t* _set_regs[N_GPIO_SET_CLR_REGS];
        volatile uint32_t* _clr_regs[N_GPIO_SET_CLR_REGS];
        volatile uint32_t* _lvl_regs[N_GPIO_LVL_REGS];

        // Store initial values prior to setting up GPIO so that we can clean
        // up nicely.
        uint32_t _orig_mode[N_GPIO_MODE_REGS];
        uint32_t _orig_set[N_GPIO_SET_CLR_REGS];
        uint32_t _orig_clr[N_GPIO_SET_CLR_REGS];
        uint32_t _orig_lvl[N_GPIO_LVL_REGS];

        AddressSpaceInfo _asi;
};
