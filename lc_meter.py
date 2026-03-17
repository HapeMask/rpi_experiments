from math import pi

from custom_viewbox import get_si_prefixes
from peripheral_interfaces import FrequencyCounter


def solve_L_x0_C_x0(freq_open, freq_cal, C_cal=1e-9):
    freq_open2 = freq_open ** 2
    freq_cal2 = freq_cal ** 2
    sq_diff = (freq_open2 - freq_cal2)

    L_x0 = sq_diff / (4 * pi ** 2 * C_cal * freq_open2 * freq_cal2)
    C_x0 = (C_cal * freq_cal2) / sq_diff

    return L_x0, C_x0


def solve_C_x_calibrated(freq, L_x0, C_x0):
    return (1 / (L_x0 * (2 * pi * freq) ** 2)) - C_x0


def solve_L_x_calibrated(freq, L_x0, C_x0):
    return (1 / (C_x0 * (2 * pi * freq) ** 2)) - L_x0


if __name__ == "__main__":
    L_x0, C_x0 = solve_L_x0_C_x0(554.7e3, 398.1e3)
    lx0_pref, _, lx0_scale = get_si_prefixes(L_x0)
    cx0_pref, _, cx0_scale = get_si_prefixes(C_x0)

    print(f"L_x0: {L_x0 / lx0_scale:0.3f} {lx0_pref}H")
    print(f"C_x0: {C_x0 / cx0_scale:0.3f} {cx0_pref}F")

    counter = FrequencyCounter(int(1e6))
    freq = counter.sample()

    C_x = solve_C_x_calibrated(freq, L_x0, C_x0)
    cx_pref, _, cx_scale = get_si_prefixes(C_x)
    print(f"C_x: {C_x / cx_scale:0.3f} {cx_pref}F")
