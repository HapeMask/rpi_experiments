from typing import List, Sequence

print("Imports...")
import sys

import numpy as np
from PyQt6.QtWidgets import (
    QApplication,
    QButtonGroup,
    QCheckBox,
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFrame,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QRadioButton,
    QSlider,
    QVBoxLayout,
    QWidget,
)
from PyQt6 import QtCore, QtGui
from PyQt6.QtCore import QTimer
import pyqtgraph as pg

from adc_interfaces import TrigMode
from adcs import ADC3908, ADC1175, ADS7884
from custom_viewbox import CustomViewBox, MinSizeMainWindow, ViewMode


AVAILABLE_SAMPLE_RATES = [int(v * 1e6) for v in [1e-2, 1e-1, 1, 2.5, 5, 10, 20, 31.25, 40, 50, 62.5]]
FSR_RANGES_10X = [0.33, 1.0, 3.3, 10.0, 50, 100.0, 180.0]
AVAILABLE_BUFFER_SIZES = [512, 1024, 2048, 4096, 8192, 16384, 32767, 65535, 131072, 262144]

# Colors for oscilloscope channels (Ch0, Ch1, ...)
CHANNEL_COLORS = ["#33ee66", "#00aeff", "#ff6633", "#ffdd00", "#cc44ff", "#ff88aa"]

# In LA mode: practical limit based on CB memory (2 CBs * 32 bytes per sample).
# 65535 samples = ~4 MB of CB memory, which is comfortable on an RPi.
LA_MAX_SAMPLES = 65535


def sample_rate_to_msps_str(sample_rate):
    return f"{sample_rate / 1e6:2.2f} MS/s"


class Oscilloscope(QApplication):
    def __init__(
        self,
        argv,
        adc,
        update_fps: int = 30,
        init_sample_rate: int = int(5e6),
        sample_rates: Sequence[int] = AVAILABLE_SAMPLE_RATES,
        graph_antialias_factor: int = 8,
    ) -> None:
        super().__init__(argv)

        self.adc = adc
        self.update_fps = update_fps
        self.n_channels = adc.n_channels
        self.sample_rates = sample_rates
        self.graph_antialias_factor = graph_antialias_factor
        self.la_mode = False
        self.osc_lines : List[pg.PlotDataItem] = []
        self._last_gen = None
        self.paused = False
        self.trig_mode = TrigMode.RISING_EDGE
        self.update_interval_sec = 1 / update_fps

        self._configure_dark_mode()

        self.window = MinSizeMainWindow(minimum_size=(700, 400))
        self.window.setWindowTitle("Oscilloscope")

        self.view_box = CustomViewBox()
        self.graph = pg.PlotWidget(viewBox=self.view_box)
        self.graph.showGrid(x=True, y=True)

        self.timer = QTimer()
        self.timer.timeout.connect(self.plot_callback)
        self.timer.start(int(self.update_interval_sec * 1000))

        trig_gbox = self._build_trigger_box()
        right_box = self._build_right_pane()
        left_box = self._build_left_pane(trig_gbox)

        layout = QHBoxLayout()
        layout.addLayout(left_box)
        layout.addWidget(self.graph)
        layout.addLayout(right_box)

        self._build_trig_line()
        self.update_trig_line_visibility()

        central_widget = QWidget()
        central_widget.setLayout(layout)
        self.window.setCentralWidget(central_widget)

        self._apply_init_defaults(init_sample_rate)
        self.reset_graph_range()

        self.window.move(100, 100)
        self.window.show()

    def _configure_dark_mode(self):
        # Taken from PyQtGraph mkQApp.
        # Determines if dark mode is active on startup. Also connects event
        # handlers to keep dark mode status in sync with the OS.
        try:
            # This only works in Qt 6.5+
            darkMode = self.styleHints().colorScheme() == QtCore.Qt.ColorScheme.Dark
            self.styleHints().colorSchemeChanged.connect(self._onColorSchemeChange)
        except AttributeError:
            darkMode = self._palette_is_dark(self.palette())
            self.paletteChanged.connect(self._onPaletteChange)
        self._set_dark_mode(darkMode)

    def _build_trigger_box(self):
        trig_gbox = QGroupBox("Trigger Options")
        trig_gbox_layout = QGridLayout()
        trig_gbox.setLayout(trig_gbox_layout)

        self.trig_bgrp = QButtonGroup(trig_gbox)
        trig_rising_radio = QRadioButton("Rising")
        trig_falling_radio = QRadioButton("Falling")
        trig_none_radio = QRadioButton("None")
        self.trig_auto_checkbox = QCheckBox("Auto")

        self.trig_bgrp.addButton(trig_none_radio)
        self.trig_bgrp.addButton(trig_rising_radio)
        self.trig_bgrp.addButton(trig_falling_radio)
        trig_rising_radio.setChecked(True)

        trig_rising_radio.mode = TrigMode.RISING_EDGE
        trig_falling_radio.mode = TrigMode.FALLING_EDGE
        trig_none_radio.mode = TrigMode.NONE
        self.trig_bgrp.buttonClicked.connect(self.trig_button_callback)

        self.show_trig_line_checkbox = QPushButton("Show Thresh.")
        self.show_trig_line_checkbox.setCheckable(True)
        self.show_trig_line_checkbox.setChecked(True)
        self.show_trig_line_checkbox.toggled.connect(self.update_trig_line_visibility)
        self.trig_auto_checkbox.stateChanged.connect(self.update_trig_line_visibility)

        trig_gbox_layout.addWidget(trig_rising_radio, 0, 0)
        trig_gbox_layout.addWidget(trig_falling_radio, 0, 1)
        trig_gbox_layout.addWidget(trig_none_radio, 1, 0)
        trig_gbox_layout.addWidget(self.trig_auto_checkbox, 1, 1)
        trig_gbox_layout.addWidget(self.show_trig_line_checkbox, 2, 0, 1, 2)

        return trig_gbox

    def _build_right_pane(self):
        right_box = QVBoxLayout()

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
        for ch_idx in range(self.n_channels):
            color = CHANNEL_COLORS[ch_idx % len(CHANNEL_COLORS)]
            ch_toggle = QPushButton(f"Ch. {ch_idx}")
            ch_toggle.setCheckable(True)
            ch_toggle.setChecked(False)
            ch_toggle.clicked.connect(lambda _UNUSED, ch=ch_idx: self.toggle_channel(ch))
            ch_toggle.setStyleSheet(f"QPushButton:checked {{background-color: {color};}}")
            channel_hbox.addWidget(ch_toggle)
            self.channel_toggles.append(ch_toggle)

        self.la_mode_button = QPushButton("LA Mode")
        self.la_mode_button.setCheckable(True)
        self.la_mode_button.setChecked(False)
        self.la_mode_button.clicked.connect(self.toggle_la_mode)
        self.la_mode_button.setStyleSheet("QPushButton:checked {background-color: #ff6633;}")

        self._add_labeled(right_box, "Sample Rate", self.sample_rate_input)
        right_box.addLayout(channel_hbox)
        right_box.addWidget(self.la_mode_button)
        self._add_labeled(right_box, "Sample Buffer", self.sample_buffer_input)

        bias_gbox = QGroupBox("Channel Bias")
        bias_outer = QHBoxLayout()
        bias_gbox.setLayout(bias_outer)
        self.bias_sliders: List[QSlider] = []
        self.bias_value_labels: List[QLabel] = []
        center = QtCore.Qt.AlignmentFlag.AlignHCenter
        for ch_idx in range(self.n_channels):
            color = CHANNEL_COLORS[ch_idx % len(CHANNEL_COLORS)]
            name_label = QLabel(f"Ch. {ch_idx + 1}")
            name_label.setStyleSheet(f"color: {color};")
            name_label.setAlignment(center)
            slider = QSlider(QtCore.Qt.Orientation.Vertical)
            slider.setRange(-1000, 1000)
            slider.setValue(0)
            slider.valueChanged.connect(
                lambda v, ch=ch_idx: self._on_bias_slider_changed(ch, v)
            )
            value_label = QLabel("+0.00V")
            value_label.setAlignment(center)

            col = QVBoxLayout()
            col.setSpacing(4)
            col.addWidget(name_label)
            col.addWidget(slider)
            col.addWidget(value_label)
            bias_outer.addLayout(col)
            self.bias_sliders.append(slider)
            self.bias_value_labels.append(value_label)
        right_box.addWidget(bias_gbox)

        right_box.addStretch(1)

        if hasattr(self.adc, "set_input_fullscale_range"):
            self.channel_config_button = QPushButton("Channel Config...")
            self.channel_config_button.clicked.connect(self._open_channel_config)
            right_box.addWidget(self.channel_config_button)
            self._build_channel_config_dialog()
        else:
            self.channel_config_button = None
            self.channel_config_dialog = None
            self.channel_fsr_combos: List[QComboBox] = []
            self.channel_fsr_status_labels: List[QLabel] = []

        return right_box

    def _build_left_pane(self, trig_gbox):
        left_box = QVBoxLayout()

        self.pause_button = QPushButton("Pause")
        self.pause_button.setCheckable(True)
        self.pause_button.clicked.connect(self.toggle_paused)

        self.trig_oneshot_button = QPushButton("One-shot")
        self.trig_oneshot_button.setCheckable(True)

        reset_zoom_button = QPushButton("Reset Zoom")
        reset_zoom_button.clicked.connect(self.reset_graph_range)

        pan_zoom_box = QHBoxLayout()
        pan_zoom_bgrp = QButtonGroup(pan_zoom_box)
        pan_radio = QRadioButton("Pan")
        zoom_radio = QRadioButton("Zoom")
        pan_zoom_bgrp.addButton(pan_radio)
        pan_zoom_bgrp.addButton(zoom_radio)
        pan_zoom_box.addWidget(pan_radio)
        pan_zoom_box.addWidget(zoom_radio)
        pan_zoom_bgrp.buttonClicked.connect(self.pan_zoom_callback)
        pan_radio.setChecked(True)
        pan_radio.mode = ViewMode.PAN
        zoom_radio.mode = ViewMode.ZOOM

        left_box.addWidget(trig_gbox)
        left_box.addWidget(self.pause_button)
        left_box.addWidget(self.trig_oneshot_button)
        left_box.addWidget(reset_zoom_button)
        left_box.addLayout(pan_zoom_box)

        return left_box

    def _build_trig_line(self):
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

    def _apply_init_defaults(self, init_sample_rate):
        self.set_sample_rate(init_sample_rate)
        self.channel_toggles[0].setChecked(True)
        self.toggle_channel(0)
        self.resize_sample_buffer(self.adc.n_samples)
        if self.channel_config_button is not None:
            default_fsr_idx = FSR_RANGES_10X.index(3.3)
            for ch in range(self.n_channels):
                combo = self.channel_fsr_combos[ch]
                combo.blockSignals(True)
                combo.setCurrentIndex(default_fsr_idx)
                combo.blockSignals(False)
                self._apply_channel_fsr(ch)

    def _set_dark_mode(self, dark_mode: bool):
        self.setProperty("darkMode", dark_mode)

    @staticmethod
    def _palette_is_dark(palette) -> bool:
        window_text = palette.color(QtGui.QPalette.ColorRole.WindowText).lightness()
        window = palette.color(QtGui.QPalette.ColorRole.Window).lightness()
        return window_text > window

    def _onColorSchemeChange(self, colorScheme):
        self._set_dark_mode(colorScheme == QtCore.Qt.ColorScheme.Dark)

    def _onPaletteChange(self, palette):
        self._set_dark_mode(self._palette_is_dark(palette))

    def _build_channel_config_dialog(self):
        self.channel_config_dialog = QDialog(self.window)
        self.channel_config_dialog.setWindowTitle("Channel Configuration")
        self.channel_config_dialog.setModal(True)

        grid = QGridLayout()
        grid.setHorizontalSpacing(0)
        grid.setVerticalSpacing(0)
        grid.addWidget(self._make_row_cell(QLabel("Channel")), 0, 0)
        grid.addWidget(self._make_row_cell(QLabel("10x Probe")), 0, 2)
        grid.addWidget(self._make_row_cell(QLabel("Input Range")), 0, 4)

        self.channel_fsr_combos = []
        self.channel_fsr_status_labels = []

        for ch in range(self.n_channels):
            bg = QtGui.QColor(CHANNEL_COLORS[ch % len(CHANNEL_COLORS)])
            bg.setAlpha(60)

            grid.addWidget(self._make_row_cell(QLabel(f"Ch. {ch}"), bg), ch + 1, 0)

            checkbox = QCheckBox()
            checkbox.setChecked(self.adc.probe_10x(ch))
            checkbox.toggled.connect(
                lambda checked, c=ch: self._on_channel_probe_toggled(c, checked)
            )
            grid.addWidget(
                self._make_row_cell(checkbox, bg, align=QtCore.Qt.AlignmentFlag.AlignCenter),
                ch + 1, 2,
            )

            combo = QComboBox()
            combo.currentIndexChanged.connect(
                lambda _idx, c=ch: self._apply_channel_fsr(c)
            )
            grid.addWidget(self._make_row_cell(combo, bg), ch + 1, 4)
            self.channel_fsr_combos.append(combo)

            status = QLabel()
            status.setStyleSheet("color: #cc3333;")
            grid.addWidget(self._make_row_cell(status, bg), ch + 1, 6)
            self.channel_fsr_status_labels.append(status)

            self._populate_channel_fsr_combo(ch)

        for col in (1, 3, 5):
            vline = QFrame()
            vline.setFrameShape(QFrame.Shape.VLine)
            vline.setFrameShadow(QFrame.Shadow.Sunken)
            grid.addWidget(vline, 0, col, self.n_channels + 1, 1)

        button_box = QDialogButtonBox(QDialogButtonBox.StandardButton.Close)
        button_box.rejected.connect(self.channel_config_dialog.close)
        button_box.accepted.connect(self.channel_config_dialog.close)

        dialog_layout = QVBoxLayout()
        dialog_layout.addLayout(grid)
        dialog_layout.addWidget(button_box)
        self.channel_config_dialog.setLayout(dialog_layout)

    @staticmethod
    def _add_labeled(box, text, widget):
        box.addWidget(QLabel(text))
        box.addWidget(widget)

    def _make_row_cell(self, widget, bg_color=None, align=None):
        container = QFrame()
        if bg_color is not None:
            container.setAutoFillBackground(True)
            palette = container.palette()
            palette.setColor(QtGui.QPalette.ColorRole.Window, bg_color)
            container.setPalette(palette)
        layout = QHBoxLayout(container)
        layout.setContentsMargins(8, 4, 8, 4)
        if align is not None:
            layout.addWidget(widget, alignment=align)
        else:
            layout.addWidget(widget)
        return container

    def _open_channel_config(self):
        self.channel_config_dialog.exec()

    def _populate_channel_fsr_combo(self, ch):
        combo = self.channel_fsr_combos[ch]
        prev_idx = combo.currentIndex()
        combo.blockSignals(True)
        combo.clear()
        for fsr in FSR_RANGES_10X:
            display_fsr = fsr if self.adc.probe_10x(ch) else fsr / 10
            combo.addItem(f"±{display_fsr:g}V", display_fsr)
        combo.setCurrentIndex(prev_idx)
        combo.blockSignals(False)

    def _on_channel_probe_toggled(self, ch, checked):
        self.adc.set_probe_10x(ch, checked)
        self._populate_channel_fsr_combo(ch)
        self._apply_channel_fsr(ch)

    def _on_bias_slider_changed(self, ch, slider_value):
        # TODO: finish.
        bias_v = 0.2 * (slider_value / 1000.0)
        self.adc.set_channel_bias(ch, bias_v)
        self.bias_value_labels[ch].setText(f"{bias_v:+.2f}V")

    def _apply_channel_fsr(self, ch):
        combo = self.channel_fsr_combos[ch]
        fsr_peak = combo.currentData()
        if fsr_peak is None:
            return
        self.channel_fsr_status_labels[ch].setText("")
        self.adc.set_input_fullscale_range(ch, fsr_peak)

    def _recreate_plot_lines(self):
        self.graph.clear()
        n_ch = self.adc.n_active_channels() if self.la_mode else self.n_channels
        self.osc_lines = []
        for ch_idx in range(n_ch):
            color = CHANNEL_COLORS[ch_idx % len(CHANNEL_COLORS)]
            line = self.graph.plot([], [], pen=pg.mkPen(color, width=1))
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
            # Pick the biggest FSR across active channels based on gain/bias.
            #if self.adc.n_active_channels() < 1:
            #    return

            fsrs = [
                self.adc.input_fullscale_range(ch)
                for ch in range(self.n_channels)
                if self.adc.channel_active(ch)
            ]
            fsr_min = min(fsr[0] for fsr in fsrs)
            fsr_max = max(fsr[1] for fsr in fsrs)
            self.graph.setYRange(fsr_min, fsr_max)

    def toggle_channel(self, channel_idx):
        self.adc.toggle_channel(channel_idx)

        self.sample_buffer_input.blockSignals(True)
        for size in AVAILABLE_BUFFER_SIZES:
            if self.sample_buffer_input.findData(size) < 0:
                self.sample_buffer_input.addItem(str(size), size)
        self.sample_buffer_input.blockSignals(False)

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
        skip_samples = 0
        if isinstance(self.adc, ADC1175):
            # TODO: Hack. Things get less reliable for early samples at high sample
            # rates with this ADC.
            skip_samples = int(10e-6 * self.adc_sample_rate)
        elif isinstance(self.adc, ADC3908):
            # TODO: And apparently this ADC too, albeit a little better?
            skip_samples = int(1e-6 * self.adc_sample_rate)

        low_thresh = 0.5
        high_thresh = 2.5
        use_trig_line = (
            (not self.trig_auto_checkbox.isChecked())
            and (self.trig_mode != TrigMode.NONE)
        )
        if use_trig_line:
            low_thresh = float(self.trig_line.value())
            high_thresh = float(self.trig_line.value())

        screen_width = self.graph.width()
        if screen_width <= 0:
            screen_width = 800

        x_range = tuple(self.graph.getViewBox().viewRange()[0])

        # TODO: Allow triggering on user's choice of channel instead of always ch1?
        low_thresh = self.adc.real_to_adc_fs(low_thresh, 0)
        high_thresh = self.adc.real_to_adc_fs(high_thresh, 0)

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
        samples = [
            self.adc.adc_fs_to_real(samples[ch], ch)
            for ch in range(self.n_channels)
        ]

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
            if not self.la_mode and not self.adc.channel_active(ch_idx):
                continue

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
    init_sample_rate = int(5e6)
    sample_rates = AVAILABLE_SAMPLE_RATES

    if "-adc1175" in sys.argv:
        adc = ADC1175()
    elif "-ads7884" in sys.argv:
        init_sample_rate = int(1e6)
        sample_rates = [int(v * 1e6) for v in [0.5, 1, 1.5, 2]]
        adc = ADS7884()
    else:
        adc = ADC3908()

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
