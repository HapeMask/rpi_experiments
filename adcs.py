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


ADC1175_FULLSCALE_VPP: Tuple[float, float] = (0.6, 2.6)

class ADC1175(ParallelADC):
    def __init__(
        self,
        VREF: Tuple[float, float],
        n_samples: int=16384,
        n_channels: int=2,
        input_range: Tuple[float, float] = (-8.08, 8.08),
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
