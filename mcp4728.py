from typing import List, Optional, Tuple

import smbus


DAC_BITS: int = 12
MAX_CODE: int = int(2 ** DAC_BITS - 1)
INTERNAL_VREF: float = 2.048


class MCP4728:
    def __init__(self, Vdd: float, address: int = 0x60) -> None:
        """Interface for the MCP4728 quad 12-bit DAC connected via I2C."""
        self.Vdd: float = Vdd
        self.address = address
        self.bus = smbus.SMBus(1)

        self.max_voltage_internal = INTERNAL_VREF * (MAX_CODE / 2 ** DAC_BITS)
        self.max_voltage_external = Vdd * (MAX_CODE / 2 ** DAC_BITS)

        self.cur_voltages: List[float] = [0, 0, 0, 0]
        self.set_voltages(*self.cur_voltages)

    def _gain_vref_settings(self, v: float) -> Tuple[int, int, float]:
        """ Choose gain / reference settings for a given voltage, preferring
        the internal reference wherever possible since Vdd may be unreliable.

        Bit values:

        gain = 0: 1x gain
        gain = 1: 2x gain

        vref = 0: Vdd
        vref = 1: internal (2.048V)
        """

        assert v <= max(2 * self.max_voltage_internal, self.max_voltage_external)

        vref = 0
        gain = 0
        max_v = self.max_voltage_external

        if v <= 2 * self.max_voltage_internal:
            vref = 1
            max_v = self.max_voltage_internal

            if v > self.max_voltage_internal:
                gain = 1
                max_v *= 2

        return gain, vref, max_v

    def _get_update_bytes(self, channel: int, v: float) -> List[int]:
        assert channel < 4
        gain_bit, vref_bit, max_v = self._gain_vref_settings(v)
        v_int = int(MAX_CODE * v / max_v)

        b0 = 0b01000000 | (channel << 1)
        b1 = (vref_bit << 7) | (gain_bit << 4) | (v_int >> 8)
        b2 = (v_int & 0xff)

        return [b0, b1, b2]

    def set_voltages(
        self,
        v0: Optional[float] = None,
        v1: Optional[float] = None,
        v2: Optional[float] = None,
        v3: Optional[float] = None
    ) -> None:
        """Sets the voltages for all channels. If a voltage for a channel is
        not given, that channel will keep its current setting."""
        tgt_voltages: List[float] = [
            (v if v is not None else cur_v)
            for v, cur_v in zip([v0, v1, v2, v3], self.cur_voltages)
        ]

        to_update = [(i, v) for i, v in enumerate(tgt_voltages) if self.cur_voltages[i] != v]
        if len(to_update) < 1:
            return

        chan_data_blocks: List[List[int]] = [self._get_update_bytes(*iv) for iv in to_update]
        data: List[int] = [b for block in chan_data_blocks for b in block]

        self.bus.write_i2c_block_data(self.address, data[0], data[1:])
        self.cur_voltages = tgt_voltages
