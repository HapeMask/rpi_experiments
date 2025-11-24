print("Imports...")
import sys

import numpy as np
from PyQt5.QtWidgets import (
    QWidget,
    QRadioButton,
    QVBoxLayout,
    QHBoxLayout,
    QApplication,
    QButtonGroup,
    QPushButton,
)
from PyQt5.QtCore import QTimer
import pyqtgraph as pg
from ads7884 import get_spi_flag_bits, DMAADC
from custom_viewbox import CustomViewBox


VDD = 5.23
SPI_HZ = 30000000
TRIGGER_TYPES = ["none", "rising_edge", "falling_edge"]
RDIV = [0.991e6, 0.999e5]

# Rough guess for the sample rate so we can pick a buffer size based on the
# target display FPS.
hack_scales = {
    4000000: 1.045,
    8000000: 1.045,
    16000000: 1.115,
    20000000: 1.045,
    24000000: 1.045,
    28000000: 1.115,
    30000000: 1.045,
    32000000: 1.115,
}
APPX_ADC_SPS = hack_scales[SPI_HZ] * int(SPI_HZ / 16)


class Oscilloscope(QApplication):
    def __init__(self, argv, update_fps=30, adc_buffer_size=4096):
        super().__init__(argv)

        self.update_fps = update_fps

        self.update_interval_sec = 1 / update_fps
        self.appx_samples_per_frame = int(APPX_ADC_SPS * self.update_interval_sec)
        #self.adc_buffer_size = min(self.appx_samples_per_frame, 32767)
        self.adc_buffer_size = adc_buffer_size

        print("ADC buffer size:", self.adc_buffer_size)

        self.adc = DMAADC(SPI_HZ, get_spi_flag_bits(clk_pha=1), VDD, self.adc_buffer_size)

        self.window = QWidget()
        self.window.setWindowTitle("Oscilloscope")
        self.window.setGeometry(100, 100, 700, 300)
        layout = QHBoxLayout(self.window)

        self.view_box = CustomViewBox()
        self.graph = pg.PlotWidget(viewBox=self.view_box)
        self.x = np.linspace(0, self.update_interval_sec, self.adc_buffer_size)
        self.y = np.zeros((self.adc_buffer_size,), np.float32)
        self.osc_line = self.graph.plot(self.x, self.y, pen=pg.mkPen("g", width=1))
        self.graph.showGrid(x=True, y=True)

        self.reset_graph_range()

        #self.graph.getPlotItem().addItem(
        #    pg.InfiniteLine(
        #        pos=1.5,
        #        angle=0,
        #        movable=True,
        #        pen=pg.mkPen(width=2, color="r"),
        #    )
        #)

        self.timer = QTimer()
        self.timer.timeout.connect(self.plot_callback)
        self.timer.start(int(self.update_interval_sec * 1000))

        right_box = QVBoxLayout()
        left_box = QVBoxLayout()

        trig_bgrp = QButtonGroup(right_box)
        trig_none_radio = QRadioButton("none")
        trig_rising_radio = QRadioButton("rising_edge")
        trig_falling_radio = QRadioButton("falling_edge")

        trig_bgrp.addButton(trig_none_radio)
        trig_bgrp.addButton(trig_rising_radio)
        trig_bgrp.addButton(trig_falling_radio)
        trig_none_radio.setChecked(True)

        right_box.addWidget(trig_none_radio)
        right_box.addWidget(trig_rising_radio)
        right_box.addWidget(trig_falling_radio)

        trig_bgrp.buttonClicked.connect(self.trig_button_callback)

        pause_button = QPushButton("Pause")
        oneshot_button = QPushButton("One-Shot")
        reset_zoom_button = QPushButton("Reset Zoom")

        pause_button.setCheckable(True)
        oneshot_button.setCheckable(True)

        pause_button.clicked.connect(self.toggle_paused)
        oneshot_button.clicked.connect(self.toggle_trig_oneshot)
        reset_zoom_button.clicked.connect(self.reset_graph_range)

        pan_zoom_box = QHBoxLayout()
        pan_zoom_bgrp = QButtonGroup(pan_zoom_box)
        pan_radio = QRadioButton("pan")
        zoom_radio = QRadioButton("zoom")
        pan_zoom_bgrp.addButton(pan_radio)
        pan_zoom_bgrp.addButton(zoom_radio)
        pan_zoom_box.addWidget(pan_radio)
        pan_zoom_box.addWidget(zoom_radio)
        pan_zoom_bgrp.buttonClicked.connect(self.pan_zoom_callback)

        left_box.addWidget(pause_button)
        left_box.addWidget(oneshot_button)
        left_box.addWidget(reset_zoom_button)
        left_box.addLayout(pan_zoom_box)

        layout.addLayout(left_box)
        layout.addWidget(self.graph)
        layout.addLayout(right_box)

        self.window.setLayout(layout)
        self.window.show()
        #self.window.keyPressEvent = self.keypress

        self.pause_button = pause_button
        self.oneshot_button = oneshot_button
        self.trig_bgrp = trig_bgrp

        self.trig_oneshot = False
        self.trig_type = "none"
        self.trig_auto_range = False
        self.paused = False

        self.adc.start_sampling()

    def reset_graph_range(self):
        self.graph.setXRange(0, self.adc_buffer_size / APPX_ADC_SPS)
        self.graph.setYRange(0, VDD)

    def toggle_paused(self):
        self.paused = not self.paused
        self.pause_button.setChecked(self.paused)

        #if self.paused:
        #    self.pause_button.setStyleSheet("background-color: #fff")
        #else:
        #    self.pause_button.setStyleSheet("background-color: #ccc")

    def toggle_trig_oneshot(self):
        self.trig_oneshot = not self.trig_oneshot
        self.oneshot_button.setChecked(self.trig_oneshot)

        #if self.trig_oneshot:
        #    self.oneshot_button.setStyleSheet("background-color: #fff")
        #else:
        #    self.oneshot_button.setStyleSheet("background-color: #ccc")

    def sample_osc(self):
        trig_range = None if self.trig_auto_range else (0.5, 2.5)

        samples, timestamps = self.adc.get_buffers()
        # TODO: Fix.
        samples = samples[3:]
        timestamps = timestamps[3:]

        samples = np.asarray(samples)
        timestamps = np.asarray(timestamps)

        #TODO: Hack.
        timestamps = timestamps.astype(np.float32) / APPX_ADC_SPS

        if trig_range is not None:
            low, high = trig_range
        else:
            sample_range = samples.max() - samples.min()
            low = 0.2 * sample_range
            high = 0.8 * sample_range

        # TODO: Move triggering into C++ code.
        trig_start = None
        triggered = False

        if self.trig_type == "rising_edge":
            for i, v in enumerate(samples):
                if v <= low:
                    trig_start = i

                if v >= high and trig_start is not None:
                    triggered = True
                    break
        elif self.trig_type == "falling_edge":
            for i, v in enumerate(samples):
                if v >= high:
                    trig_start = i

                if v <= low and trig_start is not None:
                    triggered = True
                    break

        if triggered:
            timestamps -= timestamps[trig_start]

        return samples, timestamps, triggered

    def plot_callback(self):
        if self.paused:
            return

        samples, timestamps, triggered = self.sample_osc()
        self.osc_line.setData(timestamps, samples)

        if self.trig_oneshot and triggered:
            self.toggle_paused()

    def trig_button_callback(self, button):
        self.trig_type = button.text()

    def pan_zoom_callback(self, button):
        self.view_box.set_mode(button.text())


def main():
    print("Setting up app...")
    #pg.setConfigOptions(leftButtonPan=False)
    app = Oscilloscope(sys.argv)

    print("Starting ADC and app...")
    ret = app.exec_()

    app.adc.stop_sampling()
    sys.exit(ret)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        exit()
