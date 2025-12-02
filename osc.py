print("Imports...")
import os
import sys

import numpy as np
from PyQt5.QtWidgets import (
    QApplication,
    QButtonGroup,
    QCheckBox,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    #QSpinBox,
    QComboBox,
    QLabel,
    QPushButton,
    QRadioButton,
    QVBoxLayout,
    QWidget,
)
from PyQt5.QtCore import QTimer
import pyqtgraph as pg
from ads7884 import ParallelADC
from custom_viewbox import CustomViewBox


SMI_HZ = 5000000
SPI_HZ = 31000000
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


def sample_rate_to_msps_str(sample_rate):
    return f"{sample_rate / 1e6:2.2f} MS/s"


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
    def __init__(self, argv, adc, update_fps=30, n_channels=2):
        super().__init__(argv)

        self.adc = adc
        self.update_fps = update_fps
        self.n_channels = n_channels

        self.update_interval_sec = 1 / update_fps

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
        trig_gbox_layout = QGridLayout()
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

        trig_gbox_layout.addWidget(trig_rising_radio, 0, 0)
        trig_gbox_layout.addWidget(trig_falling_radio, 0, 1)
        trig_gbox_layout.addWidget(trig_none_radio, 1, 0)
        trig_gbox_layout.addWidget(trig_auto_checkbox, 1, 1)

        #self.sample_rate_input = QSpinBox()
        self.sample_rate_input = QComboBox()
        for rate in range(int(1e6), int(50e6) + 1, 10000):
            if (int(500e6) % rate) == 0:
                self.sample_rate_input.addItem(sample_rate_to_msps_str(rate), rate)
        self.sample_rate_input.setEditable(False)
        self.sample_rate_input.currentIndexChanged.connect(
            lambda idx: self.set_sample_rate(self.sample_rate_input.itemData(idx))
        )

        channel_hbox = QHBoxLayout()
        self.channel_toggles = []
        for ch_idx in range(n_channels):
            ch_toggle = QPushButton(f"Ch. {ch_idx}")
            ch_toggle.setCheckable(True)
            ch_toggle.setChecked(False)
            ch_toggle.clicked.connect(lambda _UNUSED, ch=ch_idx: self.toggle_channel(ch))
            ch_toggle.setStyleSheet("QPushButton:checked {background-color: #00aeff;}")
            channel_hbox.addWidget(ch_toggle)
            self.channel_toggles.append(ch_toggle)

        right_box.addWidget(QLabel("Sample Rate"))
        right_box.addWidget(self.sample_rate_input)
        right_box.addLayout(channel_hbox)
        right_box.addStretch(1)
        right_box.addWidget(trig_gbox)

        trig_rising_radio.mode = "rising_edge"
        trig_falling_radio.mode = "falling_edge"
        trig_none_radio.mode = "none"

        trig_bgrp.buttonClicked.connect(self.trig_button_callback)

        pause_button = QPushButton()
        trig_oneshot_button = QPushButton()
        reset_zoom_button = QPushButton()

        pause_button.setCheckable(True)
        trig_oneshot_button.setCheckable(True)
        pause_button.setFlat(True)
        trig_oneshot_button.setFlat(True)
        reset_zoom_button.setFlat(True)

        pause_button.clicked.connect(self.toggle_paused)
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
        left_box.addWidget(trig_oneshot_button)
        left_box.addWidget(reset_zoom_button)
        left_box.addLayout(pan_zoom_box)

        layout.addLayout(left_box)
        layout.addWidget(self.graph)
        layout.addLayout(right_box)

        self.window.setLayout(layout)
        self.window.show()

        self.pause_button = pause_button
        self.trig_oneshot_button = trig_oneshot_button
        self.trig_bgrp = trig_bgrp
        self.trig_auto_checkbox = trig_auto_checkbox

        set_icon_css(trig_rising_radio, "resources/trig_rising.png", 64)
        set_icon_css(trig_falling_radio, "resources/trig_falling.png", 64)
        set_icon_css(trig_none_radio, "resources/trig_none.png", 64)
        set_icon_css(trig_auto_checkbox, "resources/trig_auto.png", 64)
        set_icon_css(pause_button, "resources/pause.png", 64)
        set_icon_css(trig_oneshot_button, "resources/trig_oneshot.png", 64)
        set_icon_css(reset_zoom_button, "resources/reset_zoom.png", 64)
        set_icon_css(pan_radio, "resources/pan.png", 64)
        set_icon_css(zoom_radio, "resources/zoom.png", 64)

        self.trig_mode = "rising_edge"
        self.paused = False

        self.set_sample_rate(SMI_HZ)
        self.toggle_channel(0)
        self.channel_toggles[0].setChecked(True)
        self.reset_graph_range()

    def set_sample_rate(self, sample_rate):
        self.adc_sample_rate = self.adc.start_sampling(sample_rate)

        sample_rate_idx = self.sample_rate_input.findData(sample_rate)
        if sample_rate_idx < 0:
            raise KeyError(f"Sample rate {sample_rate} not found in dropdown.")

        self.sample_rate_input.blockSignals(True)
        self.sample_rate_input.setCurrentIndex(sample_rate_idx)
        self.sample_rate_input.blockSignals(False)

    def reset_graph_range(self):
        self.graph.setXRange(0, self.adc.n_samples / self.adc_sample_rate)
        vref = self.adc.VREF
        self.graph.setYRange(vref[0], vref[1])

    def toggle_channel(self, channel_idx):
        self.adc.toggle_channel(channel_idx)

        toggle_btn = self.channel_toggles[channel_idx]

        # 50MS/s is only available in single-channel mode for now (due to
        # memory bandwidth limitations?). If we have more than 1 channel
        # active, remove it and switch to 25MS/s if it was selected. Otherwise
        # add it back in.
        idx_50msps = self.sample_rate_input.findData(50000000)

        n_active_channels = self.adc.n_active_channels()
        ch0_active = self.channel_toggles[0].isChecked()

        if (n_active_channels == 1 and ch0_active and idx_50msps < 0):
            self.sample_rate_input.addItem(sample_rate_to_msps_str(50000000), 50000000)
        elif (n_active_channels > 1 and idx_50msps >= 0):
            # If we removed 50MS/s and it was selected, drop down to 25MS/s.
            if self.adc_sample_rate == 50000000:
                self.set_sample_rate(25000000)
            self.sample_rate_input.removeItem(idx_50msps)

    def toggle_paused(self):
        self.paused = not self.paused
        self.pause_button.setChecked(self.paused)

    def sample_osc(self):
        samples = self.adc.get_buffers()

        # shape: [n_ch, n_samples, 2]
        samples = np.asarray(samples)
        samples, timestamps = samples[..., 0], samples[..., 1]

        # TODO: Hack. Things get less reliable for the first many samples at high sample rates.
        sample_cut_idx = int(15e-6 * self.adc_sample_rate)
        samples = samples[:, sample_cut_idx:]
        timestamps = timestamps[:, sample_cut_idx:]

        # TODO: For now assume we only have one channel...
        samples = samples[0]
        timestamps = timestamps[0]

        # TODO: Hack.
        #timestamps = cal_scales[SPI_HZ] * timestamps.astype(np.float32) / self.adc_sample_rate
        timestamps = timestamps.astype(np.float32) / self.adc_sample_rate

        if self.trig_auto_checkbox.isChecked():
            samp_min = samples.min()
            samp_max = samples.max()
            sample_range = samp_max - samp_min
            low = samp_min + 0.2 * sample_range
            high = samp_min + 0.8 * sample_range
        else:
            low, high = (0.5, 2.5)

        # TODO: Move triggering into C++ code?
        trig_start = None
        triggered = False

        # TODO: Per-channel trigger modes?
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
        if self.paused or self.adc.n_active_channels() < 1:
            return

        samples, timestamps, triggered = self.sample_osc()
        self.osc_line.setData(timestamps, samples)

        if self.trig_oneshot_button.isChecked() and triggered:
            self.toggle_paused()

    def trig_button_callback(self, button):
        self.trig_mode = button.mode

    def pan_zoom_callback(self, button):
        self.view_box.set_mode(button.mode)


def main():
    adc = ParallelADC(VREF=(0.0, 5.23), n_samples=4096)

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
