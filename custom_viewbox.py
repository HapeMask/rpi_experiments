from PyQt5 import QtCore
import pyqtgraph as pg
import numpy as np


class CustomViewBox(pg.ViewBox):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.mode = "pan"

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

        # Scale or translate based on mouse button
        if self.mode == "pan":
            tr = self.childGroup.transform()
            tr = pg.functions.invertQTransform(tr)
            tr = tr.map(diff) - tr.map(pg.Point(0, 0))

            self._resetTarget()
            self.translateBy(x=tr.x(), y=tr.y())
            self.sigRangeChangedManually.emit([True, True])
        elif self.mode == "zoom":
            if event.isFinish():
                self.rbScaleBox.hide()
                ax = QtCore.QRectF(pg.Point(event.buttonDownPos(event.button())), pg.Point(pos))
                ax = self.childGroup.mapRectFromParent(ax)
                self.showAxRect(ax)
                self.axHistoryPointer += 1
                self.axHistory = self.axHistory[:self.axHistoryPointer] + [ax]
            else:
                self.updateScaleBox(event.buttonDownPos(), event.pos())
        else:
            assert False, self.mode
