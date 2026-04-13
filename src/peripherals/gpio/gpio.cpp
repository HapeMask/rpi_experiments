#include "peripherals/gpio/gpio.hpp"

GPIO::GPIO() : Peripheral(GPIO_BASE_OFS, GPIO_LEN) {
    for(uint32_t i=0; i < N_GPIO_SET_CLR_REGS; ++i) {
        _set_regs[i] = reg_addr(GPIO_OUT_SET_OFS) + i;
        _clr_regs[i] = reg_addr(GPIO_OUT_CLR_OFS) + i;
    }

    for(uint32_t i=0; i < N_GPIO_MODE_REGS; ++i) {
        _mode_regs[i] = reg_addr(GPIO_MODE_OFS) + i;
    }

    for(uint32_t i=0; i < N_GPIO_LVL_REGS; ++i) {
        _lvl_regs[i] = reg_addr(GPIO_LVL_OFS) + i;
    }

    push_regs();
}

GPIO::~GPIO() {
    // Clean up nicely by restoring initial values in the registers.
    pop_regs();
}

volatile uint32_t* GPIO::get_mode_reg(int pin) const {
    return _mode_regs[pin / 10];
}

volatile uint32_t* GPIO::get_set_reg(int pin) const {
    return _set_regs[pin / 32];
}

volatile uint32_t* GPIO::get_clr_reg(int pin) const {
    return _clr_regs[pin / 32];
}

volatile uint32_t* GPIO::get_lvl_reg(int pin) const {
    return _lvl_regs[pin / 32];
}

void GPIO::set_pin(int pin) const {
    *get_set_reg(pin) = 1 << (pin % 32);
}

void GPIO::clear_pin(int pin) const {
    *get_clr_reg(pin) = 1 << (pin % 32);
}

bool GPIO::get_level(int pin) const {
    return (*get_lvl_reg(pin)) & (1 << (pin % 32));
}

void GPIO::set_mode(int pin, GPIOMode mode) const {
    auto mode_reg = get_mode_reg(pin);

    // Each pin gets 3 mode bits. Zero out the 3 target mode bits before
    // overwriting w/new mode.
    const uint32_t shift = (pin % 10) * 3;
    const uint32_t mask = ~(0b111 << shift);
    (*mode_reg) = ((*mode_reg) & mask) | ((uint32_t)mode << shift);
}

void GPIO::push_regs() {
    if (!_virt_regs_ptr) { return; }

    uint32_t val;

    for(uint32_t i=0; i < N_GPIO_SET_CLR_REGS; ++i) {
        val = *_set_regs[i];
        _orig_set[i].push(val);
        val = *_clr_regs[i];
        _orig_clr[i].push(val);
    }

    for(uint32_t i=0; i < N_GPIO_MODE_REGS; ++i) {
        val = *_mode_regs[i];
        _orig_mode[i].push(val);
    }
}

void GPIO::pop_regs() {
    if (!_virt_regs_ptr) { return; }

    for(uint32_t i=0; i < N_GPIO_SET_CLR_REGS; ++i) {
        if (!_orig_set[i].empty()) {
            *_set_regs[i] = _orig_set[i].top();
            _orig_set[i].pop();
        }
        if (!_orig_clr[i].empty()) {
            *_clr_regs[i] = _orig_clr[i].top();
            _orig_clr[i].pop();
        }
    }

    for(uint32_t i=0; i < N_GPIO_MODE_REGS; ++i) {
        if (!_orig_mode[i].empty()) {
            *_mode_regs[i] = _orig_mode[i].top();
            _orig_mode[i].pop();
        }
    }
}
