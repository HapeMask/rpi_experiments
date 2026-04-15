import math
from typing import Tuple, Union

import numpy as np

from adc_interfaces import ParallelADC, SerialADC
from mcp4728 import MCP4728


def map_v(
    v: Union[float, np.ndarray],
    frm: Tuple[float, float],
    to: Tuple[float, float]
) -> float:
    v = (v - frm[0]) / (frm[1] - frm[0])
    return v * (to[1] - to[0]) + to[0]


def ad8337_vgain_to_mult(v_gain: Union[float, np.ndarray]) -> np.ndarray:
    # Piecewise model of the dB curve
    gain_db = np.where(
        v_gain <= -0.6,
        0.0,

        # Linear region: slope ≈ 24 dB / 1.2 V = 20 dB/V
        # Passes through (0 V, 12.1 dB)
        np.where(v_gain >= 0.6,
            24.2,
            12.1 + 20 * v_gain,
        )
    )

    return 10 ** (gain_db / 20)


FDA_GAIN_R: Tuple[float, float] = (390, 82)
ATT_GAIN_R: Tuple[float, float] = (976e3, 22e3)
GAIN_OUTPUT_RANGE: Tuple[float, float] = (-5/6, 5/6)
BIAS_OUTPUT_RANGE: Tuple[float, float] = (-1, 1)
ADC3908_FULLSCALE_VPP: Tuple[float, float] = (-0.95, 0.95)


class ADC3908(ParallelADC):
    def __init__(
        self,
        n_samples: int=16384,
        n_channels: int=2,
        bit_format: int = 1,
        dac_Vdd: float = 2.5,
    ):
        super().__init__(
            VREF=ADC3908_FULLSCALE_VPP,
            n_samples=n_samples,
            n_channels=n_channels,
            bit_format=bit_format,
        )

        self.dac_Vdd = dac_Vdd
        self.dac = MCP4728(Vdd=dac_Vdd)
        self.channel_gain_voltages = np.array([-0.6, -0.6], dtype=np.float32)
        self.channel_bias_voltages = np.array([0, 0], dtype=np.float32)
        self._10x_mode = True
        self._45x_att = False

        self.update_dac()
        self.set_attenuation(False, False)

    def update_dac(self):
        self.dac.set_voltages(
            map_v(self.channel_gain_voltages[0], GAIN_OUTPUT_RANGE, (0, self.dac_Vdd)),
            map_v(self.channel_bias_voltages[0], BIAS_OUTPUT_RANGE, (0, self.dac_Vdd)),
            map_v(self.channel_bias_voltages[1], BIAS_OUTPUT_RANGE, (0, self.dac_Vdd)),
            map_v(self.channel_gain_voltages[1], GAIN_OUTPUT_RANGE, (0, self.dac_Vdd)),
        )

    def scale_samples(self, samples: np.ndarray) -> np.ndarray:
        if self.logic_analyzer_mode:
            return samples

        channel_mask = np.asarray([self.channel_active(i) for i in range(self.n_channels)]) > 0

        fda_gain = (FDA_GAIN_R[0] + FDA_GAIN_R[1]) / FDA_GAIN_R[1]
        vga_gain = ad8337_vgain_to_mult(self.channel_gain_voltages)

        total_gain = fda_gain * vga_gain[channel_mask]
        total_bias = self.channel_bias_voltages[channel_mask][:, None]

        if self._10x_mode:
            total_gain /= 10

        if self._45x_att:
            total_gain /= (ATT_GAIN_R[0] + ATT_GAIN_R[1]) / ATT_GAIN_R[1]

        samples = 2 * samples / total_gain[:, None] - total_bias

        return samples

    def fullscale_range(self):
        return self.scale_samples(np.asarray(ADC3908_FULLSCALE_VPP)[None]).ravel()

    def set_fullscale_range(self, fsr_peak: float) -> None:
        """Set hardware gain/attenuation to achieve the given full-scale peak voltage.

        fsr_peak is the peak input voltage in real-world units (e.g. 3.3 for ±3.3V).
        Raises ValueError if the requested FSR is not achievable with current hardware.
        """
        fda_gain    = (FDA_GAIN_R[0] + FDA_GAIN_R[1]) / FDA_GAIN_R[1]
        att_factor  = (ATT_GAIN_R[0] + ATT_GAIN_R[1]) / ATT_GAIN_R[1]
        probe_factor = 10.0 if self._10x_mode else 1.0
        # ADC input peak is 0.95 V; after scale_samples: v_real = 2*0.95 / total_gain
        required_total_gain = 1.9 / fsr_peak

        _VGA_MIN = 1.0
        _VGA_MAX = 10 ** (24.2 / 20)  # ≈ 16.22

        for use_att in (False, True):
            divisor = att_factor if use_att else 1.0
            required_vga = required_total_gain * probe_factor * divisor / fda_gain
            if _VGA_MIN <= required_vga <= _VGA_MAX:
                gain_db = 20.0 * math.log10(required_vga)
                v_gain  = (gain_db - 12.1) / 20.0
                v_gain  = max(GAIN_OUTPUT_RANGE[0], min(GAIN_OUTPUT_RANGE[1], v_gain))
                self._45x_att = use_att
                self.set_attenuation(use_att, use_att)
                self.channel_gain_voltages[:] = v_gain
                self.update_dac()
                return

        raise ValueError(
            f"±{fsr_peak}V FSR is not achievable "
            f"({'10x probe' if self._10x_mode else 'direct'} mode)"
        )


ADC1175_FULLSCALE_VPP: Tuple[float, float] = (0.6, 2.6)

class ADC1175(ParallelADC):
    def __init__(
        self,
        n_samples: int=16384,
        n_channels: int=2,
        input_range: Tuple[float, float] = (-0.66, 0.66),
    ):
        super().__init__(
            VREF=ADC1175_FULLSCALE_VPP,
            n_samples=n_samples,
            n_channels=n_channels,
            bit_format=0,
        )

        self.input_range = input_range
        self._10x_mode = True


    def update_dac(self, *args, **kwargs):
        pass

    def scale_samples(self, samples: np.ndarray) -> np.ndarray:
        if self.logic_analyzer_mode:
            return samples

        samples = map_v(samples, ADC1175_FULLSCALE_VPP, self.input_range)

        if self._10x_mode:
            samples = samples * 10

        return samples

    def fullscale_range(self):
        return self.scale_samples(np.asarray(ADC1175_FULLSCALE_VPP)[None]).ravel()
