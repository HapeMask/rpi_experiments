from typing import Tuple

from mcp4728 import MCP4728


class MultiRangeDAC:
    def __init__(self, Vdd: float) -> None:
        self.dac_range = (0.0, Vdd)
        self.bipolar_out_range = (-Vdd, Vdd)
        self.negative_out_range = (-Vdd, 0.0)
        self.dac = MCP4728(Vdd)

    def _map_v(self, v: float, rng: Tuple[float, float]) -> float:
        """Transforms a desired final output voltage into the voltage required
        from the DAC s.t. the attached op-amp will produce the desired
        output."""
        start, end = rng
        dac_start, dac_end = self.dac_range

        if v < start or v > end:
            raise ValueError(f"Invalid output voltage: {v}. Must be within [{start}, {end}].")

        v = (v - start) / (end - start)
        v = v * (dac_end - dac_start) + dac_start

        if v < dac_start or v > dac_end:
            raise ValueError(f"Invalid DAC voltage: {v}. Must be within [{dac_start}, {dac_end}].")

        return v

    def set_voltages(
        self,
        bipolar_A: float = 0.0,
        bipolar_B: float = 0.0,
        negative_A: float = 0.0,
        negative_B: float = 0.0,
    ) -> None:
        # TODO: Assumes a fixed mapping from bipolar/negative A/B to DAC
        # outputs based on hardware setup.
        self.dac.set_voltages(
            self._map_v(bipolar_A, self.bipolar_out_range),
            self._map_v(negative_A, self.negative_out_range),
            self._map_v(negative_B, self.negative_out_range),
            self._map_v(bipolar_B, self.bipolar_out_range),
        )


if __name__ == "__main__":
    import math
    import time

    dac = MultiRangeDAC(Vdd=3.3)
    f = 320
    A = 1

    start = time.time()
    try:
        while True:
            t = time.time() - start
            v = A * math.sin(2 * math.pi * t * f)

            dac.set_voltages(bipolar_A=v)

            if t > 100:
                start = time.time()
    except KeyboardInterrupt:
        dac.dac.set_voltages(0, 0, 0, 0)
