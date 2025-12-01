#include <cmath>
#include <sstream>

#include "peripherals/smi/smi.hpp"
#include "peripherals/smi/smi_defs.hpp"

// +- 10% clock target difference throws an exception.
static constexpr float MAX_REL_SPS_DIFF = 0.1;

// period (ns), setup (clks), strobe (clks), hold (clks)
std::tuple<int, int, int, int> find_best_smi_timing(uint32_t sample_rate) {
    const float tgt_sample_period_ns = 1e9f / sample_rate;

    int clk_period_ns, setup_clks, strobe_clks, hold_clks;
    float best_period_diff = 1000;
    float best_duty_cycle_diff = 1000;
    float best_sym_diff = 1000;

    bool done = false;
    for(int clk_period = 30; clk_period >= 2 && !done; clk_period -= 2) {
        for(int setup=1; setup<64 && !done; ++setup) {
            for(int strobe=1; strobe<64 && !done; ++strobe) {
                for(int hold=1; hold<64 && !done; ++hold) {
                    const float eff_sample_period_ns = clk_period * (setup + strobe + hold);
                    const float eff_duty_cycle = (float)strobe / (setup + strobe + hold);

                    // Period should be close, duty cycle should be ~0.5, and setup/hold
                    // times should be symmetric.
                    const float period_diff = std::abs(eff_sample_period_ns - tgt_sample_period_ns);
                    const float duty_cycle_diff = std::abs(eff_duty_cycle - 0.5f);
                    const float sym_diff = std::abs(setup - hold);

                    if (
                        period_diff <= best_period_diff
                        && duty_cycle_diff <= best_duty_cycle_diff
                        && sym_diff <= best_sym_diff
                    ) {
                        best_duty_cycle_diff = duty_cycle_diff;
                        best_period_diff = period_diff;
                        best_sym_diff = sym_diff;

                        clk_period_ns = clk_period;
                        setup_clks = setup;
                        strobe_clks = strobe;
                        hold_clks = hold;

                        if (period_diff == 0 && duty_cycle_diff == 0 && sym_diff == 0) {
                            done = true;
                        }
                    }
                }
            }
        }
    }

    return {clk_period_ns, setup_clks, strobe_clks, hold_clks};
}

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

SMI::~SMI() { }

uint32_t SMI::setup_timing(uint32_t tgt_sample_rate, ClockSource clk_src) {
    int clk_period_ns = 0;
    std::tie(
        clk_period_ns,
        _setup_clks,
        _strobe_clks,
        _hold_clks
    ) = find_best_smi_timing(tgt_sample_rate);

    const uint32_t sample_rate_from_smi = 1e9 / (
        clk_period_ns * (_setup_clks + _strobe_clks + _hold_clks)
    );
    const float sample_rate_diff_smi = std::abs(
        (float)sample_rate_from_smi - tgt_sample_rate
    ) / (float)tgt_sample_rate;

    if (sample_rate_diff_smi > MAX_REL_SPS_DIFF) {
        std::ostringstream ss;
        ss << "Requested sample rate: " << tgt_sample_rate
           << "Hz, but best we can do with SMI timing is: " << sample_rate_from_smi;
        throw std::runtime_error(ss.str());
    }

    const float smi_clock_hz = 1e9 / clk_period_ns;
    const auto closest_clk_hz = _clock.start_clock(ClockID::SMI, clk_src, smi_clock_hz);
    const uint32_t sample_rate_from_clk = closest_clk_hz / (
        _setup_clks + _strobe_clks + _hold_clks
    );
    const float sample_rate_diff_clk = std::abs(
        (float)sample_rate_from_clk - tgt_sample_rate
    ) / (float)tgt_sample_rate;

    if (sample_rate_diff_clk > MAX_REL_SPS_DIFF) {
        std::ostringstream ss;
        ss << "Requested sample rate: " << tgt_sample_rate
           << "Hz, but best we can do with the given clock source is: " << sample_rate_from_clk;
        throw std::runtime_error(ss.str());
    }

    return sample_rate_from_clk;
}

void SMI::setup_device_settings(SMIWidth xfer_width_bits, uint32_t device_id, bool use_dma) {
    if (_strobe_clks < 1 || _hold_clks < 1 || _setup_clks < 1) {
        throw std::runtime_error(
            "setup_device_settings() called before setup_timing()."
        );
    }

    _cs_reg->bits = 0;

    _read_cfg_regs[device_id]->bits = SMIDeviceConfigRead{{
        .strobe=_strobe_clks,
        .hold=_hold_clks,
        .setup=_setup_clks,
        .width=xfer_width_bits
    }}.bits;

    _write_cfg_regs[device_id]->bits = SMIDeviceConfigWrite{{
        .strobe=_strobe_clks,
        .hold=_hold_clks,
        .setup=_setup_clks,
        .width=xfer_width_bits
    }}.bits;

    if (use_dma) {
        _dma_ctl_reg->bits = SMIDMAControl{{
            .dma_req_thresh_write=4,
            .dma_req_thresh_read=4,
            .panic_write=8,
            .panic_read=8,
            .dma_enable=1
        }}.bits;
    }
}

void SMI::start_xfer(int n_samples, bool packed) {
    *_len_reg = n_samples;

    _cs_reg->bits = SMIControlStatus{{
        .enable=1,
        .start=1,
        .clear=1,
        .pxldat=packed
    }}.bits;
}

void SMI::stop_xfer() {
    _cs_reg->bits = 0;
}
