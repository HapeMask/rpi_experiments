from typing import Tuple
from math import log10

from PyQt6 import QtCore
from PyQt6.QtWidgets import QMainWindow, QGraphicsItem, QGraphicsSimpleTextItem
from PyQt6.QtGui import QColor, QBrush

import pyqtgraph as pg


def get_si_prefixes(val: float) -> Tuple[str, str, float]:
    pref = ""
    inv_pref = ""
    scale = 1

    if val == 0:
        return pref, inv_pref, scale

    val = abs(val)
    flip = False

    if val < 1:
        flip = True
        val = 1 / val

    lval = round(log10(val))

    if lval >= 9:
        pref = "G"
        inv_pref = "n"
        scale = 1e9
    elif lval >= 6:
        pref = "M"
        inv_pref = "u"
        scale = 1e6
    elif lval >= 3:
        pref = "K"
        inv_pref = "m"
        scale = 1e3

    if flip:
        pref, inv_pref = inv_pref, pref

    return pref, inv_pref, scale


def format_dVdt(dV: float, dt: float) -> str:
    freq = 0 if dt == 0 else 1 / dt
    v_prefix, _, v_scale = get_si_prefixes(dV)
    p_prefix, f_prefix, t_scale = get_si_prefixes(dt)

    return (
        f"dV: {dV * v_scale:0.3f} {v_prefix}V\n"
        f"dt: {dt * t_scale:0.3f} {p_prefix}s ({freq / t_scale:0.3f} {f_prefix}Hz)"
    )


class CustomViewBox(pg.ViewBox):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.mode = "pan"

        self.scaleBoxLabel = pg.TextItem("", color="#ffffff")
        self.scaleBoxLabel.setParentItem(self.rbScaleBox)

    def set_mode(self, mode):
        self.mode = mode

        if self.mode == "pan":
            self.state["mouseMode"] = pg.ViewBox.PanMode
        elif self.mode == "zoom":
            self.state["mouseMode"] = pg.ViewBox.RectMode

    def mouseDragEvent(self, event, axis=None):
        """Copied from the base class but modified s.t. we pan or scale based
        on a toggle variable rather than mouse button type, and scaling always
        uses the rect feature. Designed for use with touchscreens."""
        event.accept()

        pos = event.pos()
        lastPos = event.lastPos()
        diff = pos - lastPos
        diff = diff * -1

        tr = self.childGroup.transform()
        tr = pg.functions.invertQTransform(tr)

        # Scale or translate based on mouse button
        if self.mode == "pan":
            diff_tr = tr.map(diff) - tr.map(pg.Point(0, 0))

            self._resetTarget()
            self.translateBy(x=diff_tr.x(), y=diff_tr.y())
            self.sigRangeChangedManually.emit([True, True])
        elif self.mode == "zoom":
            start_pos = event.buttonDownPos(event.button())
            ax = QtCore.QRectF(pg.Point(start_pos), pg.Point(pos))
            axhp = ax.height()
            ax = self.childGroup.mapRectFromParent(ax)
            dt, dV = ax.width(), ax.height()

            if pos.y() > start_pos.y():
                self.scaleBoxLabel.setAnchor((0, 1))
            else:
                self.scaleBoxLabel.setAnchor((0, 0))

            self.scaleBoxLabel.setText(format_dVdt(abs(dV), abs(dt)))

            if event.isFinish():
                self.rbScaleBox.hide()
                self.showAxRect(ax)
                self.axHistoryPointer += 1
                self.axHistory = self.axHistory[:self.axHistoryPointer] + [ax]
            else:
                self.updateScaleBox(event.buttonDownPos(), event.pos())
        else:
            assert False, self.mode


class MinSizeMainWindow(QMainWindow):
    def __init__(self, *args, minimum_size, **kwargs):
        super().__init__(*args, **kwargs)

        self.minimum_size = minimum_size
        self.setMinimumSize(*minimum_size)

    def resizeEvent(self, event):
        if (
            event.size().width() < self.minimum_size[0] or
            event.size().height() < self.minimum_size[1]
        ):
            new_size = QtCore.QSize(
                max(event.size().width(), self.minimum_size[0]),
                max(event.size().height(), self.minimum_size[1]),
            )
            self.resize(new_size)
        else:
            return super().resizeEvent(event)
