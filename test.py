import time

import numpy as np
from tqdm import tqdm
import matplotlib.pyplot as plt

from adc_interfaces import get_spi_flag_bits, SerialADC


N = 100000
VREF = (0.0, 5.23)

#val = (0x28 << 4) | (0xf0 >> 4)
#val = 0x28f
#val = 0b1010001111
#mea = 3.1982421875 V

#adc = SerialADC(8000000, get_spi_flag_bits(clk_pha=1), VREF, 1024)
#adc.start_sampling()
#time.sleep(1)
#samples, timestamps = adc.get_buffers()
#print(samples[0], timestamps[0])
#exit()

freqs = np.linspace(5e5, 48e6, 100).astype(np.int32)
vals = []
markers = []
for spi_freq in tqdm(freqs):
    spi_freq = int(spi_freq)
    adc = SerialADC(spi_freq, get_spi_flag_bits(clk_pha=1), VREF, N)

    start = time.time()
    adc.start_sampling()
    samples, timestamps = adc.get_buffers()
    end = time.time()

    adc.stop_sampling()

    msps = 1e-6 * N / (end - start)
    mval = max(samples)

    vals.append(msps)

    #assert abs(mval - VREF[1]) < 0.15, f"Bad max: {mval:0.2f}"
    if abs(mval - VREF[1]) >= 0.15:
        #print(f"Bad max: {mval:0.2f} for {spi_freq * 1e-6:0.2f} MHz")
        markers.append("x")
    else:
        #print(f"{spi_freq * 1e-6:0.2f} MHz: {msps:0.2f} Msps, max: {mval:0.2f}")
        markers.append(".")

    del adc

x = 1e-6 * freqs.astype(np.float32) / 16

print("Plotting...")
plt.plot(x, vals, "b-")
for xi, vi, mi in zip(x, vals, markers):
    plt.scatter(xi, vi, c="r" if mi == "x" else "b", marker=mi)
plt.xlabel("SPI MHz/16")
plt.ylabel("Read MS/s")
plt.xlim(0.0, 3.2)
plt.ylim(0.0, 1.1)
plt.tight_layout()
print("Saving...")
plt.savefig("msps.png")
