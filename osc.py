from typing import Sequence

print("Imports...")
import os
import sys

import numpy as np
from PyQt6.QtWidgets import (
    QApplication,
    QButtonGroup,
    QCheckBox,
    QComboBox,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QRadioButton,
    QVBoxLayout,
    QWidget,
)
from PyQt6 import QtCore, QtGui
from PyQt6.QtCore import QTimer
import pyqtgraph as pg

from adc_interfaces import TrigMode
from adcs import ADC3908, ADC1175, SerialADC
from peripheral_interfaces import get_spi_flag_bits
from custom_viewbox import CustomViewBox, MinSizeMainWindow


AVAILABLE_SAMPLE_RATES = [int(v * 1e6) for v in [1e-2, 1e-1, 1, 2.5, 5, 10, 20, 31.25, 40, 50, 62.5]]
AVAILABLE_BUFFER_SIZES = [512, 1024, 2048, 4096, 8192, 16384, 32767, 65535, 131072, 262144]

# Colors for oscilloscope channels (Ch0, Ch1, ...)
CHANNEL_COLORS = ["#33ee66", "#00aeff", "#ff6633", "#ffdd00", "#cc44ff", "#ff88aa"]

# In LA mode: practical limit based on CB memory (2 CBs * 32 bytes per sample).
# 65535 samples = ~4 MB of CB memory, which is comfortable on an RPi.
LA_MAX_SAMPLES = 65535


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
    def __init__(
        self,
        argv,
        adc,
        update_fps: int = 30,
        n_channels: int = 2,
        init_sample_rate: int = int(5e6),
        sample_rates: Sequence[int] = AVAILABLE_SAMPLE_RATES,
        graph_antialias_factor: int = 8,
    ) -> None:
        super().__init__(argv)

        self.adc = adc
        self.update_fps = update_fps
        self.n_channels = n_channels
        self.sample_rates = sample_rates
        self.graph_antialias_factor = graph_antialias_factor
        self.la_mode = False
        self.osc_lines = []
        self._last_gen = None

        self.update_interval_sec = 1 / update_fps

        # Taken from PyQtGraph mkQApp.
        # Determines if dark mode is active on startup. Also connects event
        # handlers to keep dark mode status in sync with the OS.
        try:
            # This only works in Qt 6.5+
            darkMode = self.styleHints().colorScheme() == QtCore.Qt.ColorScheme.Dark
            self.styleHints().colorSchemeChanged.connect(self._onColorSchemeChange)
        except AttributeError:
            palette = self.palette()
            windowTextLightness = palette.color(QtGui.QPalette.ColorRole.WindowText).lightness()
            windowLightness = palette.color(QtGui.QPalette.ColorRole.Window).lightness()
            darkMode = windowTextLightness > windowLightness
            self.paletteChanged.connect(self._onPaletteChange)
        self.setProperty("darkMode", darkMode)

        self.window = MinSizeMainWindow(minimum_size=(700, 400))
        self.window.setWindowTitle("Oscilloscope")
        layout = QHBoxLayout()

        self.view_box = CustomViewBox()
        self.graph = pg.PlotWidget(viewBox=self.view_box)
        self.graph.showGrid(x=True, y=True)

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

        show_trig_line_checkbox = QPushButton("Show Thresh.")
        show_trig_line_checkbox.setCheckable(True)
        show_trig_line_checkbox.setChecked(True)

        trig_gbox_layout.addWidget(trig_rising_radio, 0, 0)
        trig_gbox_layout.addWidget(trig_falling_radio, 0, 1)
        trig_gbox_layout.addWidget(trig_none_radio, 1, 0)
        trig_gbox_layout.addWidget(trig_auto_checkbox, 1, 1)
        trig_gbox_layout.addWidget(show_trig_line_checkbox, 2, 0, 1, 2)

        self.sample_rate_input = QComboBox()
        for rate in self.sample_rates:
            self.sample_rate_input.addItem(sample_rate_to_msps_str(rate), rate)
        self.sample_rate_input.setEditable(False)
        self.sample_rate_input.currentIndexChanged.connect(
            lambda idx: self.set_sample_rate(self.sample_rate_input.itemData(idx))
        )

        self.sample_buffer_input = QComboBox()
        for size in AVAILABLE_BUFFER_SIZES:
            self.sample_buffer_input.addItem(str(size), size)
        self.sample_buffer_input.setEditable(False)
        self.sample_buffer_input.currentIndexChanged.connect(
            lambda idx: self.resize_sample_buffer(self.sample_buffer_input.itemData(idx))
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

        la_mode_button = QPushButton("LA Mode")
        la_mode_button.setCheckable(True)
        la_mode_button.setChecked(False)
        la_mode_button.clicked.connect(self.toggle_la_mode)
        la_mode_button.setStyleSheet("QPushButton:checked {background-color: #ff6633;}")
        self.la_mode_button = la_mode_button

        right_box.addWidget(QLabel("Sample Rate"))
        right_box.addWidget(self.sample_rate_input)
        right_box.addLayout(channel_hbox)
        right_box.addWidget(la_mode_button)
        right_box.addWidget(QLabel("Sample Buffer"))
        right_box.addWidget(self.sample_buffer_input)
        right_box.addStretch(1)
        right_box.addWidget(trig_gbox)

        trig_rising_radio.mode = TrigMode.RISING_EDGE
        trig_falling_radio.mode = TrigMode.FALLING_EDGE
        trig_none_radio.mode = TrigMode.NONE

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

        self.pause_button = pause_button
        self.trig_oneshot_button = trig_oneshot_button
        self.trig_bgrp = trig_bgrp
        self.trig_auto_checkbox = trig_auto_checkbox
        self.trig_auto_checkbox.stateChanged.connect(self.update_trig_line_visibility)

        self.show_trig_line_checkbox = show_trig_line_checkbox
        self.show_trig_line_checkbox.toggled.connect(self.update_trig_line_visibility)

        set_icon_css(trig_rising_radio, "resources/trig_rising.png", 64)
        set_icon_css(trig_falling_radio, "resources/trig_falling.png", 64)
        set_icon_css(trig_none_radio, "resources/trig_none.png", 64)
        set_icon_css(trig_auto_checkbox, "resources/trig_auto.png", 64)
        set_icon_css(pause_button, "resources/pause.png", 64)
        set_icon_css(trig_oneshot_button, "resources/trig_oneshot.png", 64)
        set_icon_css(reset_zoom_button, "resources/reset_zoom.png", 64)
        set_icon_css(pan_radio, "resources/pan.png", 64)
        set_icon_css(zoom_radio, "resources/zoom.png", 64)

        trig_line_color = "#FA234A"
        self.trig_line = pg.InfiniteLine(
            pos=2,
            angle=0,
            movable=True,
            pen=pg.mkPen(width=2, color=trig_line_color, dash=[4, 4]),
        )
        self.trig_line_label = pg.InfLineLabel(
            self.trig_line, "{value:0.2f}V", color=trig_line_color, position=0.1
        )
        self.trig_line_label.textItem.setTextInteractionFlags(
            QtCore.Qt.TextInteractionFlag.NoTextInteraction
        )
        self.trig_line_label.setAcceptHoverEvents(False)
        self.trig_line_label.textItem.setAcceptHoverEvents(False)

        self.trig_mode = TrigMode.RISING_EDGE
        self.update_trig_line_visibility()
        self.paused = False

        central_widget = QWidget()
        central_widget.setLayout(layout)
        self.window.setCentralWidget(central_widget)

        self.set_sample_rate(init_sample_rate)
        self.channel_toggles[0].setChecked(True)
        self.toggle_channel(0)
        self.resize_sample_buffer(adc.n_samples)

        self.reset_graph_range()

        self.window.move(100, 100)
        self.window.show()

    def _onColorSchemeChange(self, colorScheme):
        # Attempt to keep darkMode attribute up to date
        # QEvent.Type.PaletteChanged/ApplicationPaletteChanged will be emitted before
        # QStyleHint().colorSchemeChanged.emit()!
        # Uses Qt 6.5+ API
        darkMode = colorScheme == QtCore.Qt.ColorScheme.Dark
        self.setProperty("darkMode", darkMode)

    def _onPaletteChange(self, palette):
        # Attempt to keep darkMode attribute up to date
        # QEvent.Type.PaletteChanged/ApplicationPaletteChanged will be emitted after
        # paletteChanged.emit()!
        # Using API deprecated in Qt 6.0
        windowTextLightness = palette.color(QtGui.QPalette.ColorRole.WindowText).lightness()
        windowLightness = palette.color(QtGui.QPalette.ColorRole.Window).lightness()
        darkMode = windowTextLightness > windowLightness
        self.setProperty("darkMode", darkMode)

    def _recreate_plot_lines(self):
        """Clear and recreate plot lines for the current number of active channels."""
        self.graph.clear()
        n_ch = self.adc.n_active_channels()
        dummy = np.zeros(max(self.adc.n_samples, 1), np.float32)
        self.osc_lines = []
        for ch_idx in range(n_ch):
            color = CHANNEL_COLORS[ch_idx % len(CHANNEL_COLORS)]
            line = self.graph.plot(dummy, dummy, pen=pg.mkPen(color, width=1))
            self.osc_lines.append(line)
        self.graph.getPlotItem().addItem(self.trig_line)

    def resize_sample_buffer(self, n_samples):
        buffer_size_idx = self.sample_buffer_input.findData(n_samples)
        if buffer_size_idx < 0:
            raise KeyError(f"Sample count {n_samples} not found in dropdown.")

        self.adc.resize(n_samples)
        self.adc_sample_rate = self.adc.start_sampling(self.adc_sample_rate)
        self._recreate_plot_lines()

        self.sample_buffer_input.blockSignals(True)
        self.sample_buffer_input.setCurrentIndex(buffer_size_idx)
        self.sample_buffer_input.blockSignals(False)

    def set_sample_rate(self, sample_rate):
        if sample_rate not in self.sample_rates:
            raise ValueError(f"Requested sample rate not available: {sample_rate}")

        sample_rate_idx = self.sample_rate_input.findData(sample_rate)
        if sample_rate_idx < 0:
            raise KeyError(f"Sample rate {sample_rate} not found in dropdown.")

        self.adc_sample_rate = self.adc.start_sampling(sample_rate)

        self.sample_rate_input.blockSignals(True)
        self.sample_rate_input.setCurrentIndex(sample_rate_idx)
        self.sample_rate_input.blockSignals(False)

    def reset_graph_range(self):
        self.graph.setXRange(0, self.adc.n_samples / self.adc_sample_rate)
        if self.la_mode:
            n_ch = self.adc.n_active_channels()
            self.graph.setYRange(-0.25, n_ch)
        else:
            # Pick the biggest FSR across channels based on gain/bias. TODO:
            # Can / should we have separate scales for each channel? Seems
            # confusing.
            fsr = self.adc.fullscale_range()
            self.graph.setYRange(fsr[0::2].min(), fsr[1::2].max())

    def toggle_channel(self, channel_idx):
        self.adc.toggle_channel(channel_idx)

        # >=50MS/s is only available in single-channel mode (only Ch. 0 active)
        # due to SMI memory bandwidth limitations.
        n_active_channels = self.adc.n_active_channels()
        ch0_active = self.channel_toggles[0].isChecked()
        is_single_channel_mode = (n_active_channels == 1 and ch0_active)

        sample_rates = self.sample_rates
        buffer_sizes = AVAILABLE_BUFFER_SIZES

        self.sample_rate_input.blockSignals(True)
        self.sample_buffer_input.blockSignals(True)

        if is_single_channel_mode:
            # Restore all options that may have been removed.
            for rate in sample_rates:
                if self.sample_rate_input.findData(rate) < 0:
                    self.sample_rate_input.addItem(sample_rate_to_msps_str(rate), rate)
            for size in buffer_sizes:
                if self.sample_buffer_input.findData(size) < 0:
                    self.sample_buffer_input.addItem(str(size), size)
        else:
            # Remove sample rates >=50MS/s (not supported in dual-channel mode).
            for idx in range(self.sample_rate_input.count() - 1, -1, -1):
                if self.sample_rate_input.itemData(idx) >= int(50e6):
                    self.sample_rate_input.removeItem(idx)
            # Restore any buffer sizes that were previously removed.
            for size in buffer_sizes:
                if self.sample_buffer_input.findData(size) < 0:
                    self.sample_buffer_input.addItem(str(size), size)

        self.sample_rate_input.blockSignals(False)
        self.sample_buffer_input.blockSignals(False)

        if not is_single_channel_mode and self.adc_sample_rate >= int(50e6):
            self.set_sample_rate(int(20e6))

        self._recreate_plot_lines()

    def toggle_la_mode(self):
        self.la_mode = self.la_mode_button.isChecked()
        self.adc.set_logic_analyzer_mode(self.la_mode, 8)

        # Channel toggles only apply in oscilloscope mode
        for toggle in self.channel_toggles:
            toggle.setEnabled(not self.la_mode)

        self.sample_buffer_input.blockSignals(True)
        if self.la_mode:
            # LA mode DMA len is 16-bit; >= 16384 samples * 4 bytes overflows
            for idx in range(self.sample_buffer_input.count() - 1, -1, -1):
                if self.sample_buffer_input.itemData(idx) > LA_MAX_SAMPLES:
                    self.sample_buffer_input.removeItem(idx)
            # Configure PWM at current sample rate for paced GPIO reads
            self.adc_sample_rate = self.adc.start_sampling(self.adc_sample_rate)
        else:
            for size in AVAILABLE_BUFFER_SIZES:
                if self.sample_buffer_input.findData(size) < 0:
                    self.sample_buffer_input.addItem(str(size), size)
        self.sample_buffer_input.blockSignals(False)

        if self.la_mode and self.adc.n_samples > LA_MAX_SAMPLES:
            max_allowed = self.sample_buffer_input.itemData(self.sample_buffer_input.count() - 1)
            self.resize_sample_buffer(max_allowed)
        else:
            self.resize_sample_buffer(self.adc.n_samples)
            self._recreate_plot_lines()

        if self.la_mode:
            self.adc.stop_sampling()
        else:
            self.adc_sample_rate = self.adc.start_sampling(self.adc_sample_rate)

        self.update_trig_line_visibility()
        self.reset_graph_range()

    def toggle_paused(self):
        self.paused = not self.paused
        self.pause_button.setChecked(self.paused)

    def update_trig_line_visibility(self):
        # The trig_line should be visible when the auto trigger checkbox is
        # not checked, the trigger mode is not "none", and not in LA mode
        visible = (
            (not self.la_mode)
            and (not self.trig_auto_checkbox.isChecked())
            and (self.trig_mode != TrigMode.NONE)
            and self.show_trig_line_checkbox.isChecked()
        )
        self.trig_line.setVisible(visible)

    def sample_osc(self):
        if isinstance(self.adc, ADC1175):
            # TODO: Hack. Things get less reliable for early samples at high sample
            # rates with this ADC.
            skip_samples = int(10e-6 * self.adc_sample_rate)
        else:
            skip_samples = 0

        low_thresh = 0.5
        high_thresh = 2.5
        use_trig_line = (
            (not self.trig_auto_checkbox.isChecked())
            and (self.trig_mode != TrigMode.NONE)
        )
        if use_trig_line:
            low_thresh = self.trig_line.value()
            high_thresh = self.trig_line.value()

        screen_width = self.graph.width()
        if screen_width <= 0:
            screen_width = 800

        x_range = tuple(self.graph.getViewBox().viewRange()[0])

        buffers, triggered, _trig_start = self.adc.get_buffers(
            screen_width=self.graph_antialias_factor * screen_width,
            x_range=x_range,
            auto_range=self.trig_auto_checkbox.isChecked(),
            thresh=(low_thresh, high_thresh),
            trig_mode=self.trig_mode,
            skip_samples=skip_samples,
        )

        # shape: [n_ch, screen_width, 2] — last dim is [value, time_seconds]
        # Timestamps are already trigger-adjusted and in seconds.
        samples, timestamps = buffers[..., 0], buffers[..., 1]
        timestamps = timestamps[0]

        # samples shape: [n_ch, screen_width]
        samples = self.adc.scale_samples(samples)

        return samples, timestamps, triggered

    def plot_callback(self):
        if self.paused or self.adc.n_active_channels() < 1:
            return

        current_gen = self.adc.data_generation
        if current_gen == self._last_gen:
            return
        self._last_gen = current_gen

        samples, timestamps, triggered = self.sample_osc()

        for ch_idx, line in enumerate(self.osc_lines):
            if ch_idx >= samples.shape[0]:
                break
            ch_samples = samples[ch_idx]
            if self.la_mode:
                # Bit i spans [i, i+0.5]: scale 0/1 to 0/0.5, then offset by bit index
                ch_samples = ch_samples * 0.5 + ch_idx
            line.setData(timestamps, ch_samples)

        if self.trig_oneshot_button.isChecked() and triggered:
            self.toggle_paused()

    def trig_button_callback(self, button):
        self.trig_mode = button.mode
        self.update_trig_line_visibility()

    def pan_zoom_callback(self, button):
        self.view_box.set_mode(button.mode)


def main():
    if "-s" in sys.argv:
        sys.argv.remove("-s")
        init_sample_rate = int(1e6)
        sample_rates = [int(v * 1e6) for v in [0.5, 1, 1.5, 2]]

        adc = SerialADC(get_spi_flag_bits(clk_pha=1), VREF=(0, 3.3), n_samples=2048)
    else:
        init_sample_rate = int(5e6)
        sample_rates = AVAILABLE_SAMPLE_RATES

        # For old scope
        if "-adc1175" in sys.argv:
            adc = ADC1175()
        else:
            # For new scope
            adc = ADC3908()
            adc.update_dac()

    print("Setting up app...")
    pg.setConfigOptions(antialias=True)
    app = Oscilloscope(sys.argv, adc, init_sample_rate=init_sample_rate, sample_rates=sample_rates)

    print("Starting app...")
    ret = app.exec()

    app.adc.stop_sampling()
    sys.exit(ret)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        exit()
