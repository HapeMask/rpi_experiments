import math
from typing import overload, Tuple, Union

import numpy as np

from adc_interfaces import ParallelADC, SerialADC
from mcp4728 import MCP4728


@overload
def map_v(v: float, frm: Tuple[float, float], to: Tuple[float, float]) -> float: ...
@overload
def map_v(v: np.ndarray, frm: Tuple[float, float], to: Tuple[float, float]) -> np.ndarray: ...

def map_v(
    v: Union[float, np.ndarray],
    frm: Tuple[float, float],
    to: Tuple[float, float]
) -> Union[float, np.ndarray]:
    v = (v - frm[0]) / (frm[1] - frm[0])
    return v * (to[1] - to[0]) + to[0]


# TODO: Actually calibrate this. Right now it works for 3.3V.
AD8337_CAL_FACTOR = 1.1

@overload
def ad8337_vgain_to_mult(v_gain: float) -> float: ...
@overload
def ad8337_vgain_to_mult(v_gain: np.ndarray) -> np.ndarray: ...

def ad8337_vgain_to_mult(v_gain: Union[float, np.ndarray]) -> Union[float, np.ndarray]:
    # Piecewise model of the dB curve
    gain_db = np.where(
        v_gain <= -0.6, 0.0,
        # Linear region: slope ≈ 24 dB / 1.2 V = 20 dB/V
        # Passes through (0 V, 12.1 dB)
        np.where(v_gain >= 0.6, 24.2, 12.1 + 20 * v_gain)
    )

    mult = (10 ** (gain_db / 20))
    mult = mult / AD8337_CAL_FACTOR

    if isinstance(v_gain, float):
        return float(mult.squeeze())

    return mult


@overload
def ad8337_mult_to_vgain(mult: float) -> float: ...
@overload
def ad8337_mult_to_vgain(mult: np.ndarray) -> np.ndarray: ...

def ad8337_mult_to_vgain(mult: Union[float, np.ndarray]) -> Union[float, np.ndarray]:
    gain_db = np.log10(mult * AD8337_CAL_FACTOR) * 20

    v_gain = np.where(
        gain_db <= 0, -0.6,
        np.where(gain_db > 24.2, 0.6, (gain_db - 12.1) / 20)
    )

    if isinstance(mult, float):
        return float(v_gain.squeeze())

    return v_gain


FDA_GAIN_R: Tuple[float, float] = (390, 82)
ATT_GAIN_R: Tuple[float, float] = (976e3, 22e3)
GAIN_OUTPUT_RANGE: Tuple[float, float] = (-5/6, 5/6)
BIAS_OUTPUT_RANGE: Tuple[float, float] = (-1, 1)
ADC3908_FULLSCALE_RANGE: Tuple[float, float] = (-0.95, 0.95)
ADC3908_FULLSCALE_VPP = (ADC3908_FULLSCALE_RANGE[1] - ADC3908_FULLSCALE_RANGE[0])


class ADC3908(ParallelADC):
    def __init__(
        self,
        n_samples: int=16384,
        n_channels: int=2,
        bit_format: int = 1,
        dac_Vdd: float = 2.5,
    ):
        super().__init__(
            VREF=ADC3908_FULLSCALE_RANGE,
            n_samples=n_samples,
            n_channels=n_channels,
            bit_format=bit_format,
        )

        self.dac_Vdd = dac_Vdd
        self.dac = MCP4728(Vdd=dac_Vdd)
        self.channel_gain_voltages = np.array([-0.6, -0.6], dtype=np.float32)
        self.channel_bias_voltages = np.array([0, 0], dtype=np.float32)
        self._10x_mode = [True for _ in range(self.n_channels)]
        self._45x_att = [False for _ in range(self.n_channels)]

        self.update_dac()
        for channel in range(self.n_channels):
            self.set_attenuation(channel, False)

    def probe_10x(self, channel: int) -> bool:
        return self._10x_mode[channel]

    def set_probe_10x(self, channel: int, value: bool) -> None:
        self._10x_mode[channel] = value

    def update_dac(self):
        self.dac.set_voltages(
            map_v(self.channel_gain_voltages[0], GAIN_OUTPUT_RANGE, (0, self.dac_Vdd)),
            map_v(self.channel_bias_voltages[0], BIAS_OUTPUT_RANGE, (0, self.dac_Vdd)),
            map_v(self.channel_bias_voltages[1], BIAS_OUTPUT_RANGE, (0, self.dac_Vdd)),
            map_v(self.channel_gain_voltages[1], GAIN_OUTPUT_RANGE, (0, self.dac_Vdd)),
        )

    @overload
    def adc_fs_to_real(self, samples: float, channel: int, inverse: bool) -> float: ...
    @overload
    def adc_fs_to_real(self, samples: np.ndarray, channel: int, inverse: bool) -> np.ndarray: ...

    def adc_fs_to_real(
        self, samples: Union[float, np.ndarray], channel: int, inverse: bool = False,
    ) -> Union[float, np.ndarray]:
        """Map voltage samples within the ADC's fullscale range to real voltage
        levels at the oscilloscope input, or optionally perform the inverse."""

        was_float = isinstance(samples, float)

        if self.logic_analyzer_mode or not self.channel_active(channel):
            return samples

        opa_gain = 0.5 # 1/2 voltage divider used to combine DC bias signal.
        fda_gain = (FDA_GAIN_R[0] + FDA_GAIN_R[1]) / FDA_GAIN_R[1]
        vga_gain = ad8337_vgain_to_mult(self.channel_gain_voltages[channel])

        # TODO: Actual voltage is 1/2 of what the DAC+buffer puts out, and
        # applied after all attenuation.
        bias = self.channel_bias_voltages[channel]

        probe_gain = 1
        if self._10x_mode[channel]:
            probe_gain = 0.1

        att_gain = 1
        if self._45x_att[channel]:
            att_gain  = ATT_GAIN_R[1] / (ATT_GAIN_R[0] + ATT_GAIN_R[1])

        if inverse:
            samples = (samples * probe_gain * att_gain + bias) * opa_gain
            samples = samples * vga_gain * fda_gain
        else:
            samples = samples / (fda_gain * vga_gain)
            samples = (samples / opa_gain - bias) / (probe_gain * att_gain)

        if was_float:
            samples = float(samples)

        return samples

    @overload
    def real_to_adc_fs(self, samples: float, channel: int) -> float: ...
    @overload
    def real_to_adc_fs(self, samples: np.ndarray, channel: int) -> np.ndarray: ...
    def real_to_adc_fs(
        self, samples: Union[float, np.ndarray], channel: int
    ) -> Union[float, np.ndarray]:
        """Map voltage samples at the oscilloscope input to levels within the
        ADC's fullscale input range."""

        return self.adc_fs_to_real(samples, channel, inverse=True)

    def input_fullscale_range(self, channel):
        return self.adc_fs_to_real(np.asarray(ADC3908_FULLSCALE_RANGE)[None], channel).ravel()

    def set_input_fullscale_range(self, channel: int, fsr_peak: float) -> None:
        """Set hardware gain/attenuation to achieve the given full-scale peak voltage.

        fsr_peak is the peak input voltage in real-world units (e.g. 3.3 for ±3.3V).
        Raises ValueError if the requested FSR is not achievable with current hardware.
        """
        opa_gain     = 0.5
        fda_gain     = (FDA_GAIN_R[0] + FDA_GAIN_R[1]) / FDA_GAIN_R[1]
        att_gain     =  ATT_GAIN_R[1] / (ATT_GAIN_R[0] + ATT_GAIN_R[1])
        probe_gain   = 1 / 10 if self._10x_mode[channel] else 1.0
        fsr_vpp      = 2 * fsr_peak

        required_total_gain = ADC3908_FULLSCALE_VPP / fsr_vpp

        # TODO: Some of this logic resembles the logic in adc_fs_to_real above, merge?
        for use_att in (False, True):
            _att_gain = att_gain if use_att else 1.0

            # total gain = probe_gain * att_gain * opa_gain * vga_gain * fda*gain
            required_vga = required_total_gain / (probe_gain * _att_gain * opa_gain * fda_gain)
            v_gain = ad8337_mult_to_vgain(required_vga)

            if v_gain < -0.7 or v_gain > 0.7:
                continue

            if abs(required_vga - ad8337_vgain_to_mult(v_gain)) > 1e-2:
                continue

            self._45x_att[channel] = use_att
            self.set_attenuation(channel, use_att)
            self.channel_gain_voltages[channel] = v_gain
            self.update_dac()
            return

        raise ValueError(
            f"±{fsr_peak}V FSR is not achievable "
            f"({'10x' if self._10x_mode[channel] else '1x'} probe mode)"
        )


ADC1175_FULLSCALE_RANGE: Tuple[float, float] = (0.6, 2.6)
ADC1175_FULLSCALE_VPP = (ADC1175_FULLSCALE_RANGE[1] - ADC1175_FULLSCALE_RANGE[0])

class ADC1175(ParallelADC):
    def __init__(
        self,
        n_samples: int=16384,
        n_channels: int=2,
        input_range: Tuple[float, float] = (-0.66, 0.66),
    ):
        super().__init__(
            VREF=ADC1175_FULLSCALE_RANGE,
            n_samples=n_samples,
            n_channels=n_channels,
            bit_format=0,
        )

        self.input_range = input_range
        self._10x_mode = [True for _ in range(self.n_channels)]

    def probe_10x(self, channel: int) -> bool:
        return self._10x_mode[channel]

    def set_probe_10x(self, channel: int, value: bool) -> None:
        self._10x_mode[channel] = value

    def update_dac(self, *args, **kwargs):
        pass

    @overload
    def adc_fs_to_real(self, samples: float, channel: int, inverse: bool) -> float: ...
    @overload
    def adc_fs_to_real(self, samples: np.ndarray, channel: int, inverse: bool) -> np.ndarray: ...

    def adc_fs_to_real(
        self, samples: Union[float, np.ndarray], channel: int, inverse: bool = False
    ) -> Union[float, np.ndarray]:
        """Map voltage samples within the ADC's fullscale range to real voltage
        levels at the oscilloscope input, or optionally perform the inverse."""
        if self.logic_analyzer_mode:
            return samples

        if inverse:
            if self._10x_mode[channel]:
                samples = samples / 10

            samples = map_v(samples, self.input_range, ADC1175_FULLSCALE_RANGE)
        else:
            samples = map_v(samples, ADC1175_FULLSCALE_RANGE, self.input_range)

            if self._10x_mode[channel]:
                samples = samples * 10

        return samples

    @overload
    def real_to_adc_fs(self, samples: float, channel: int) -> float: ...
    @overload
    def real_to_adc_fs(self, samples: np.ndarray, channel: int) -> np.ndarray: ...
    def real_to_adc_fs(
        self, samples: Union[float, np.ndarray], channel: int
    ) -> Union[float, np.ndarray]:
        """Map voltage samples at the oscilloscope input to levels within the
        ADC's fullscale input range."""
        return self.adc_fs_to_real(samples, channel, inverse=True)

    def input_fullscale_range(self, channel):
        return self.adc_fs_to_real(np.asarray(ADC1175_FULLSCALE_RANGE)[None], channel).ravel()
