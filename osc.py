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

from adc_interfaces import ParallelADC, SerialADC
from peripheral_interfaces import get_spi_flag_bits
from custom_viewbox import CustomViewBox, MinSizeMainWindow


AVAILABLE_SAMPLE_RATES = [int(v * 1e6) for v in [1, 2.5, 5, 10, 20, 31.25, 40, 50, 62.5]]
AVAILABLE_BUFFER_SIZES = [int(2 ** size_exp) for size_exp in range(9, 16)]
AVAILABLE_BUFFER_SIZES[-1:] = [32767]

# Colors for oscilloscope channels (Ch0, Ch1, ...)
CHANNEL_COLORS = ["#33ee66", "#00aeff", "#ff6633", "#ffdd00", "#cc44ff", "#ff88aa"]

# In LA mode: vertical spacing between digital channels (in "volts" units)
LA_CHANNEL_SPACING = 1.5


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

        self.trig_mode = "rising_edge"
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
            self.graph.setYRange(-0.25, n_ch * LA_CHANNEL_SPACING)
        else:
            vref = self.adc.VREF
            self.graph.setYRange(vref[0], vref[1])

    def toggle_channel(self, channel_idx):
        self.adc.toggle_channel(channel_idx)

        # >=50MS/s is only available in single-channel mode (only Ch. 0 active)
        # for now due to memory bandwidth limitations. If we have more than 1
        # channel active, remove all higher sample rates and switch to 20MS/s
        # if one was selected. Similar to the above logic, if we are in
        # single-channel mode we can support up to 32767 samples. Otherwise, at
        # most 16384.
        n_active_channels = self.adc.n_active_channels()
        ch0_active = self.channel_toggles[0].isChecked()
        is_single_channel_mode = (n_active_channels == 1 and ch0_active)

        sample_rates = self.sample_rates
        buffer_sizes = AVAILABLE_BUFFER_SIZES

        if is_single_channel_mode:
            # Add back any previously-removed options.
            self.sample_rate_input.blockSignals(True)
            self.sample_buffer_input.blockSignals(True)

            for rate in sample_rates:
                if self.sample_rate_input.findData(rate) < 0:
                    self.sample_rate_input.addItem(sample_rate_to_msps_str(rate), rate)

            for size in buffer_sizes:
                if self.sample_buffer_input.findData(size) < 0:
                    self.sample_buffer_input.addItem(str(size), size)

            self.sample_rate_input.blockSignals(False)
            self.sample_buffer_input.blockSignals(False)
        else:
            self.sample_rate_input.blockSignals(True)
            self.sample_buffer_input.blockSignals(True)

            for idx in range(self.sample_rate_input.count() - 1, -1, -1):
                if self.sample_rate_input.itemData(idx) >= int(50e6):
                    self.sample_rate_input.removeItem(idx)

            for idx in range(self.sample_buffer_input.count() - 1, -1, -1):
                if self.sample_buffer_input.itemData(idx) >= 32767:
                    self.sample_buffer_input.removeItem(idx)

            self.sample_rate_input.blockSignals(False)
            self.sample_buffer_input.blockSignals(False)

            # If we were sampling at >=50MS/s, drop down to 20Ms/s.
            if self.adc_sample_rate >= int(50e6):
                self.set_sample_rate(int(20e6))

            # If we had a buffer size larger than the limit, pick the new
            # largest allowed.
            if self.adc.n_samples >= 32767:
                self.resize_sample_buffer(16384)
                return  # resize_sample_buffer already calls _recreate_plot_lines

        self._recreate_plot_lines()

    def toggle_la_mode(self):
        self.la_mode = self.la_mode_button.isChecked()
        self.adc.set_logic_analyzer_mode(self.la_mode, 8)

        # Channel toggles only apply in oscilloscope mode
        for toggle in self.channel_toggles:
            toggle.setEnabled(not self.la_mode)

        self._recreate_plot_lines()
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
            and (self.trig_mode != "none")
            and self.show_trig_line_checkbox.isChecked()
        )
        self.trig_line.setVisible(visible)

    def sample_osc(self):
        # TODO: Hack. Things get less reliable for early samples at high sample rates.
        sample_cut_idx = int(10e-6 * self.adc_sample_rate)

        low_thresh = 0.5
        high_thresh = 2.5
        use_trig_line = (
            (not self.trig_auto_checkbox.isChecked())
            and (self.trig_mode != "none")
        )
        if use_trig_line:
            low_thresh = self.trig_line.value()
            high_thresh = self.trig_line.value()

        screen_width = self.graph.width()
        if screen_width <= 0:
            screen_width = 800

        buffers, triggered, trig_start = self.adc.get_buffers(
            screen_width=self.graph_antialias_factor * screen_width,
            auto_range=self.trig_auto_checkbox.isChecked(),
            low_thresh=low_thresh,
            high_thresh=high_thresh,
            trig_mode=self.trig_mode,
            skip_samples=sample_cut_idx,
        )

        buffers = buffers[:, sample_cut_idx:]

        # shape: [n_ch, n_binned, 2] — last dim is [value, sample_idx]
        samples, timestamps = buffers[..., 0], buffers[..., 1]

        # All channels share the same time axis; use channel 0's timestamps.
        timestamps = timestamps[0] / self.adc_sample_rate

        if triggered and trig_start is not None:
            timestamps -= timestamps[trig_start]

        # samples shape: [n_ch, n_binned]
        return samples, timestamps, triggered

    def plot_callback(self):
        if self.paused or self.adc.n_active_channels() < 1:
            return

        samples, timestamps, triggered = self.sample_osc()

        for ch_idx, line in enumerate(self.osc_lines):
            if ch_idx >= samples.shape[0]:
                break
            ch_samples = samples[ch_idx]
            if self.la_mode:
                # Stack digital channels: channel i offset by i * spacing
                ch_samples = ch_samples + ch_idx * LA_CHANNEL_SPACING
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
        adc = SerialADC(get_spi_flag_bits(clk_pha=1), VREF=(0, 3.3), n_samples=2048)
        sample_rates = [int(v * 1e6) for v in [0.5, 1, 1.5, 2]]
    else:
        init_sample_rate = int(5e6)
        adc = ParallelADC(VREF=(-5.0, 5.0), n_samples=16384)
        sample_rates = AVAILABLE_SAMPLE_RATES

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
