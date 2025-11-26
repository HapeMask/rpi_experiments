print("Imports...")
import os
import sys

import numpy as np
from PyQt5.QtWidgets import (
    QApplication,
    QButtonGroup,
    QCheckBox,
    QGroupBox,
    QHBoxLayout,
    QPushButton,
    QRadioButton,
    QVBoxLayout,
    QWidget,
)
from PyQt5.QtCore import QTimer
import pyqtgraph as pg
from ads7884 import get_spi_flag_bits, DMAADC
from custom_viewbox import CustomViewBox


SPI_HZ = 31000000
APPX_ADC_SPS = int(SPI_HZ / 16)
cal_scales = {
    4000000:  1.045,
    8000000:  1.045,
    16000000: 1.115,
    20000000: 1.045,
    24000000: 1.045,
    28000000: 1.115,
    30000000: 1.045,
    31000000: 1.000,
    32000000: 1.115,
}


def set_icon_css(widget, icon_path, size):
    class_name = widget.__class__.__name__

    base, ext = os.path.splitext(icon_path)
    if isinstance(widget, (QRadioButton, QCheckBox)):
        css = (
            f"""{class_name}::indicator {{
    width: {size}px;
    height: {size}px;
}}

{class_name}::indicator::unchecked {{
    image: url({icon_path});
}}

{class_name}::indicator::checked {{
    image: url({base}_pressed{ext});
}}"""
        )
    else:
        css = (
            f"""{class_name} {{
    width: {size}px;
    height: {size}px;
    image: url({icon_path});
}}

{class_name}:pressed {{
    image: url({base}_pressed{ext});
}}

{class_name}:checked {{
    image: url({base}_pressed{ext});
}}"""
        )

    widget.setStyleSheet(css)


class Oscilloscope(QApplication):
    def __init__(self, argv, adc, update_fps=30):
        super().__init__(argv)

        self.adc = adc
        self.update_fps = update_fps

        self.update_interval_sec = 1 / update_fps
        #self.appx_samples_per_frame = int(APPX_ADC_SPS * self.update_interval_sec)

        self.window = QWidget()
        self.window.setWindowTitle("Oscilloscope")
        self.window.setGeometry(100, 100, 700, 300)
        layout = QHBoxLayout(self.window)

        self.view_box = CustomViewBox()
        self.graph = pg.PlotWidget(viewBox=self.view_box)
        self.x = np.linspace(0, self.update_interval_sec, self.adc.n_samples)
        self.y = np.zeros((self.adc.n_samples,), np.float32)
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

        trig_gbox = QGroupBox("Trigger Options")
        trig_gbox_layout = QVBoxLayout()
        trig_gbox.setLayout(trig_gbox_layout)

        trig_bgrp = QButtonGroup(right_box)
        trig_rising_radio = QRadioButton()
        trig_falling_radio = QRadioButton()
        trig_none_radio = QRadioButton()
        trig_auto_checkbox = QCheckBox()

        trig_bgrp.addButton(trig_none_radio)
        trig_bgrp.addButton(trig_rising_radio)
        trig_bgrp.addButton(trig_falling_radio)
        trig_rising_radio.setChecked(True)

        trig_gbox_layout.addWidget(trig_none_radio)
        trig_gbox_layout.addWidget(trig_rising_radio)
        trig_gbox_layout.addWidget(trig_falling_radio)
        trig_gbox_layout.addWidget(trig_auto_checkbox)
        right_box.addWidget(trig_gbox)

        trig_rising_radio.mode = "rising_edge"
        trig_falling_radio.mode = "falling_edge"
        trig_none_radio.mode = "none"

        trig_bgrp.buttonClicked.connect(self.trig_button_callback)
        trig_auto_checkbox.clicked.connect(self.toggle_trig_auto)

        pause_button = QPushButton()
        oneshot_button = QPushButton()
        reset_zoom_button = QPushButton()

        pause_button.setCheckable(True)
        oneshot_button.setCheckable(True)
        pause_button.setFlat(True)
        oneshot_button.setFlat(True)
        reset_zoom_button.setFlat(True)

        pause_button.clicked.connect(self.toggle_paused)
        oneshot_button.clicked.connect(self.toggle_trig_oneshot)
        reset_zoom_button.clicked.connect(self.reset_graph_range)

        pan_zoom_box = QHBoxLayout()
        pan_zoom_bgrp = QButtonGroup(pan_zoom_box)
        pan_radio = QRadioButton()
        zoom_radio = QRadioButton()
        pan_zoom_bgrp.addButton(pan_radio)
        pan_zoom_bgrp.addButton(zoom_radio)
        pan_zoom_box.addWidget(pan_radio)
        pan_zoom_box.addWidget(zoom_radio)
        pan_zoom_bgrp.buttonClicked.connect(self.pan_zoom_callback)
        pan_radio.setChecked(True)
        pan_radio.mode = "pan"
        zoom_radio.mode = "zoom"

        left_box.addWidget(pause_button)
        left_box.addWidget(oneshot_button)
        left_box.addWidget(reset_zoom_button)
        left_box.addLayout(pan_zoom_box)

        layout.addLayout(left_box)
        layout.addWidget(self.graph)
        layout.addLayout(right_box)

        self.window.setLayout(layout)
        self.window.show()

        self.pause_button = pause_button
        self.oneshot_button = oneshot_button
        self.trig_bgrp = trig_bgrp
        self.trig_auto_checkbox = trig_auto_checkbox

        set_icon_css(trig_rising_radio, "resources/trig_rising.png", 64)
        set_icon_css(trig_falling_radio, "resources/trig_falling.png", 64)
        set_icon_css(trig_none_radio, "resources/trig_none.png", 64)
        set_icon_css(trig_auto_checkbox, "resources/trig_auto.png", 64)
        set_icon_css(pause_button, "resources/pause.png", 64)
        set_icon_css(oneshot_button, "resources/trig_oneshot.png", 64)
        set_icon_css(reset_zoom_button, "resources/reset_zoom.png", 64)
        set_icon_css(pan_radio, "resources/pan.png", 64)
        set_icon_css(zoom_radio, "resources/zoom.png", 64)

        self.trig_oneshot = False
        self.trig_mode = "rising_edge"
        self.trig_auto = False
        self.paused = False

        self.adc.start_sampling()

    def reset_graph_range(self):
        self.graph.setXRange(0, self.adc.n_samples / APPX_ADC_SPS)
        self.graph.setYRange(0, self.adc.VDD)

    def toggle_paused(self):
        self.paused = not self.paused
        self.pause_button.setChecked(self.paused)

    def toggle_trig_oneshot(self):
        self.trig_oneshot = not self.trig_oneshot
        self.oneshot_button.setChecked(self.trig_oneshot)

    def toggle_trig_auto(self):
        self.trig_auto = not self.trig_auto
        self.trig_auto_checkbox.setChecked(self.trig_auto)

    def sample_osc(self):
        samples, timestamps = self.adc.get_buffers()
        samples = np.asarray(samples)[3:]
        timestamps = np.asarray(timestamps)[3:]

        #TODO: Hack.
        timestamps = cal_scales[SPI_HZ] * timestamps.astype(np.float32) / APPX_ADC_SPS

        if self.trig_auto:
            global printed
            samp_min = samples.min()
            samp_max = samples.max()
            sample_range = samp_max - samp_min
            low = samp_min + 0.2 * sample_range
            high = samp_min + 0.8 * sample_range
        else:
            low, high = (0.5, 2.5)

        # TODO: Move triggering into C++ code.
        trig_start = None
        triggered = False

        if self.trig_mode == "rising_edge":
            for i, v in enumerate(samples):
                if v <= low:
                    trig_start = i

                if v >= high and trig_start is not None:
                    triggered = True
                    break
        elif self.trig_mode == "falling_edge":
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
        self.trig_mode = button.mode

    def pan_zoom_callback(self, button):
        self.view_box.set_mode(button.mode)


def main():
    adc = DMAADC(
        spi_speed=SPI_HZ,
        spi_flag_bits=get_spi_flag_bits(clk_pha=1),
        VDD=5.23,
        n_samples=4096,
    )

    print("Setting up app...")
    app = Oscilloscope(sys.argv, adc)

    print("Starting app...")
    ret = app.exec_()

    app.adc.stop_sampling()
    sys.exit(ret)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        exit()
