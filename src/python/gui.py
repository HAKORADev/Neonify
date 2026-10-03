import os
import sys
import subprocess
import threading
import time
import datetime
import json
import tempfile
from pathlib import Path

from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QFrame, QLabel, QPushButton, QComboBox, QSlider, QCheckBox,
    QFileDialog, QProgressBar, QScrollArea, QSizePolicy,
    QMessageBox, QGroupBox, QListWidget, QStackedWidget,
    QDialog, QDoubleSpinBox, QDialogButtonBox
)
from PyQt5.QtCore import (
    Qt, QTimer, QThread, pyqtSignal, QRectF, QRect, QPoint, QUrl
)
from PyQt5.QtGui import (
    QPixmap, QImage, QPainter, QColor, QPen, QBrush, QFont,
    QPalette, QIcon
)

try:
    from PyQt5.QtMultimedia import QMediaPlayer, QMediaContent
    HAS_MULTIMEDIA = True
except ImportError:
    HAS_MULTIMEDIA = False

THEME = {
    'background': '#0A0A0A',
    'surface': '#1a1a1a',
    'surface_hover': '#2a2a2a',
    'surface_active': '#3a3a3a',
    'text': '#E5E5E5',
    'text_secondary': '#A0A0A0',
    'accent': '#F5F5F5',
    'accent_hover': '#FFFFFF',
    'accent_pressed': '#D8D8D8',
    'accent_disabled': '#3a3a3a',
    'border': '#404040',
    'border_light': '#E5E5E5',
    'border_disabled': '#555555',
    'error': '#f44336',
    'warning': '#ff9800',
    'success': '#F5F5F5',
    'panel_background': '#121212',
    'panel_border': '#E5E5E5',
    'neon_red': '#FF4466',
    'neon_blue': '#3FA9FF',
}


def get_icon_path():
    icon_name = 'logo.png'
    candidates = []
    if getattr(sys, 'frozen', False) and hasattr(sys, '_MEIPASS'):
        candidates.append(os.path.join(sys._MEIPASS, icon_name))
        candidates.append(os.path.join(os.path.dirname(os.path.abspath(sys.executable)), icon_name))
    try:
        base_dir = os.path.dirname(os.path.abspath(__file__))
    except NameError:
        base_dir = os.path.dirname(os.path.abspath(sys.argv[0]))
    candidates.append(os.path.join(base_dir, icon_name))
    candidates.append(os.path.join(base_dir, os.pardir, 'assets', icon_name))
    for icon_path in candidates:
        if os.path.exists(icon_path):
            return icon_path
    return candidates[-1]


def load_app_icon():
    icon_path = get_icon_path()
    if os.path.exists(icon_path):
        return QIcon(icon_path)
    return None


def get_main_button_style():
    return """
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #121212, stop:0.3 #121212, stop:0.7 #1a1a1a, stop:1 #121212);
            border: 2px solid #E5E5E5;
            border-radius: 8px;
            font-size: 14px;
            font-weight: bold;
            color: white;
            padding: 10px 20px;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #121212, stop:0.3 #161616, stop:0.7 #1e1e1e, stop:1 #121212);
            border: 2px solid #FFFFFF;
            color: #FFFFFF;
        }
        QPushButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #0e0e0e, stop:0.3 #121212, stop:0.7 #161616, stop:1 #0e0e0e);
            border: 2px solid #D8D8D8;
        }
        QPushButton:disabled {
            background-color: #2a2a2a;
            border: 2px solid #555555;
            color: #666666;
        }
    """


def get_secondary_button_style():
    return """
        QPushButton {
            background-color: #1a1a1a;
            color: #E5E5E5;
            border: 1px solid #404040;
            border-radius: 6px;
            font-size: 12px;
            padding: 8px 16px;
        }
        QPushButton:hover {
            background-color: #2a2a2a;
            border: 1px solid #E5E5E5;
        }
        QPushButton:pressed {
            background-color: #3a3a3a;
        }
        QPushButton:disabled {
            background-color: #1a1a1a;
            border: 1px solid #404040;
            color: #666666;
        }
    """


def get_accent_button_style():
    return """
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #c8c8c8, stop:0.5 #F5F5F5, stop:1 #c8c8c8);
            border: 2px solid #FFFFFF;
            border-radius: 8px;
            font-size: 14px;
            font-weight: bold;
            color: #0A0A0A;
            padding: 10px 20px;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #e8e8e8, stop:0.5 #FFFFFF, stop:1 #e8e8e8);
            border: 2px solid #FFFFFF;
        }
        QPushButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                stop:0 #b0b0b0, stop:0.5 #D8D8D8, stop:1 #b0b0b0);
        }
        QPushButton:disabled {
            background-color: #2a2a2a;
            border: 2px solid #555555;
            color: #666666;
        }
    """


def get_surface_button_style():
    return """
        QPushButton {
            background-color: #2a2a2a;
            color: white;
            border: 1px solid #3a3a3a;
            border-radius: 5px;
            font-size: 12px;
            padding: 6px 12px;
        }
        QPushButton:hover {
            background-color: #3a3a3a;
            border: 1px solid #E5E5E5;
        }
        QPushButton:pressed {
            background-color: #4a4a4a;
            border: 1px solid #E5E5E5;
        }
        QPushButton:disabled {
            background-color: #2a2a2a;
            border: 1px solid #404040;
            color: #666666;
        }
    """


def get_panel_style():
    return f"""
        QFrame {{
            background-color: {THEME['panel_background']};
            border: 2px solid {THEME['panel_border']};
            border-radius: 8px;
        }}
    """


def get_combo_box_style():
    return f"""
        QComboBox {{
            background-color: {THEME['surface']};
            color: {THEME['text']};
            border: 2px solid {THEME['border_light']};
            border-radius: 6px;
            padding: 6px 12px;
            min-width: 100px;
            font-size: 12px;
        }}
        QComboBox::drop-down {{
            border: none;
            subcontrol-origin: padding;
            subcontrol-position: right center;
            width: 24px;
        }}
        QComboBox::down-arrow {{
            image: none();
            width: 0px;
            height: 0px;
        }}
        QComboBox:hover {{
            border: 2px solid #FFFFFF;
        }}
        QComboBox:disabled {{
            background-color: #2a2a2a;
            border: 2px solid #555555;
            color: #666666;
        }}
        QComboBox QAbstractItemView {{
            background-color: {THEME['surface']};
            color: {THEME['text']};
            border: 1px solid {THEME['border_light']};
            selection-background-color: {THEME['surface_hover']};
            selection-color: {THEME['text']};
        }}
    """


def get_progress_bar_style():
    return f"""
        QProgressBar {{
            border: 1px solid {THEME['border']};
            background-color: {THEME['surface']};
            height: 20px;
            border-radius: 10px;
            text-align: center;
            color: {THEME['text']};
            font-weight: bold;
        }}
        QProgressBar::chunk {{
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0,
                stop:0 #a8a8a8, stop:0.5 #FFFFFF, stop:1 #a8a8a8);
            border-radius: 9px;
        }}
    """


def get_slider_style():
    return f"""
        QSlider::groove:horizontal {{
            border: 1px solid {THEME['border']};
            height: 8px;
            background: {THEME['surface']};
            border-radius: 4px;
        }}
        QSlider::handle:horizontal {{
            background: {THEME['accent']};
            border: 2px solid {THEME['text']};
            width: 18px;
            margin: -6px 0;
            border-radius: 9px;
        }}
        QSlider::handle:horizontal:hover {{
            background: #FFFFFF;
        }}
        QSlider::sub-page:horizontal {{
            background: {THEME['accent']};
            border-radius: 4px;
        }}
    """


def get_group_box_style():
    return f"""
        QGroupBox {{
            font-weight: bold;
            font-size: 13px;
            color: {THEME['text']};
            border: 1px solid {THEME['border']};
            border-radius: 6px;
            margin-top: 10px;
            padding-top: 10px;
        }}
        QGroupBox::title {{
            subcontrol-origin: margin;
            subcontrol-position: top left;
            padding: 0 8px;
            color: {THEME['text']};
        }}
    """


def get_checkbox_style():
    return f"""
        QCheckBox {{
            color: {THEME['text']};
            font-size: 12px;
            spacing: 8px;
        }}
        QCheckBox::indicator {{
            width: 18px;
            height: 18px;
            border: 2px solid {THEME['border_light']};
            border-radius: 4px;
            background-color: {THEME['surface']};
        }}
        QCheckBox::indicator:checked {{
            background-color: {THEME['accent']};
            border: 2px solid {THEME['accent']};
        }}
        QCheckBox::indicator:hover {{
            border: 2px solid {THEME['accent']};
        }}
        QCheckBox:disabled {{
            color: #666666;
        }}
        QCheckBox::indicator:disabled {{
            background-color: #2a2a2a;
            border: 2px solid #555555;
        }}
    """


def get_list_style():
    return f"""
        QListWidget {{
            background-color: {THEME['surface']};
            color: {THEME['text']};
            border: 2px solid {THEME['border']};
            border-radius: 6px;
            font-size: 12px;
            padding: 4px;
        }}
        QListWidget::item {{
            padding: 6px 8px;
            border-radius: 4px;
        }}
        QListWidget::item:selected {{
            background-color: {THEME['surface_hover']};
            color: white;
        }}
        QListWidget::item:hover {{
            background-color: {THEME['surface_active']};
        }}
    """


class ScrollableImageViewer(QScrollArea):

    def __init__(self, parent=None):
        super().__init__(parent)
        self.zoom_level = 1.0
        self.min_zoom = 0.1
        self.max_zoom = 10.0
        self.pixmap = None

        self.image_label = QLabel()
        self.image_label.setAlignment(Qt.AlignCenter)
        self.image_label.setStyleSheet(f"background-color: {THEME['surface']};")
        self.image_label.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Expanding)

        self.setWidget(self.image_label)
        self.setWidgetResizable(True)
        self.setAlignment(Qt.AlignCenter)
        self.setStyleSheet(f"""
            QScrollArea {{
                border: 1px solid {THEME['border']};
                border-radius: 6px;
                background-color: {THEME['surface']};
            }}
        """)

    def setPixmap(self, pixmap):
        self.pixmap = pixmap
        self.image_label.setStyleSheet(f"background-color: {THEME['surface']};")
        self.updateImage()

    def show_text(self, text):
        self.pixmap = None
        self.image_label.setText(text)
        self.image_label.setStyleSheet(
            f"background-color: {THEME['surface']}; color: {THEME['text_secondary']}; font-size: 13px;")

    def updateImage(self):
        if self.pixmap:
            scaled = self.pixmap.scaled(
                self.image_label.size() * self.zoom_level,
                Qt.KeepAspectRatio, Qt.SmoothTransformation
            )
            self.image_label.setPixmap(scaled)

    def setZoomLevel(self, level):
        self.zoom_level = max(self.min_zoom, min(self.max_zoom, level))
        self.updateImage()

    def wheelEvent(self, event):
        factor = 1.15 if event.angleDelta().y() > 0 else 0.87
        self.setZoomLevel(self.zoom_level * factor)


class ImageComparisonSlider(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        self.before_pixmap = None
        self.after_pixmap = None
        self.slider_position = 0.5
        self.zoom_level = 1.0
        self.pan_offset = QPoint(0, 0)
        self.dragging = False
        self.last_pos = None
        self.setMinimumSize(400, 300)
        self.setStyleSheet(f"background-color: {THEME['surface']}; border: 1px solid {THEME['border']}; border-radius: 6px;")

    def setBeforeImage(self, pixmap):
        self.before_pixmap = pixmap
        self.update()

    def setAfterImage(self, pixmap):
        self.after_pixmap = pixmap
        self.update()

    def setZoomLevel(self, level):
        self.zoom_level = max(0.1, min(10.0, level))
        self.update()

    def setSliderPosition(self, pos):
        self.slider_position = max(0.0, min(1.0, pos))
        self.update()

    def resetView(self):
        self.zoom_level = 1.0
        self.pan_offset = QPoint(0, 0)
        self.update()

    def wheelEvent(self, event):
        factor = 1.15 if event.angleDelta().y() > 0 else 0.87
        self.setZoomLevel(self.zoom_level * factor)

    def getScaledRect(self, pixmap):
        if not pixmap:
            return QRect()
        widget_size = self.size()
        scaled = pixmap.size() * self.zoom_level
        x = (widget_size.width() - scaled.width()) // 2 + self.pan_offset.x()
        y = (widget_size.height() - scaled.height()) // 2 + self.pan_offset.y()
        return QRect(x, y, scaled.width(), scaled.height())

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.setRenderHint(QPainter.SmoothPixmapTransform)
        painter.fillRect(self.rect(), QColor(THEME['surface']))
        if self.after_pixmap:
            after_rect = self.getScaledRect(self.after_pixmap)
            painter.drawPixmap(after_rect, self.after_pixmap)
        if self.before_pixmap:
            before_rect = self.getScaledRect(self.before_pixmap)
            painter.setClipRect(QRect(0, 0, int(self.width() * self.slider_position), self.height()))
            painter.drawPixmap(before_rect, self.before_pixmap)
            painter.setClipping(False)
        divider_x = int(self.width() * self.slider_position)
        painter.setPen(QPen(QColor(THEME['accent']), 2))
        painter.drawLine(divider_x, 0, divider_x, self.height())
        handle_rect = QRect(divider_x - 12, self.height() // 2 - 12, 24, 24)
        painter.setBrush(QBrush(QColor(THEME['accent'])))
        painter.setPen(QPen(QColor(THEME['border_light']), 1))
        painter.drawEllipse(handle_rect)
        painter.setPen(QPen(QColor(THEME['background']), 2))
        painter.drawLine(divider_x - 5, self.height() // 2, divider_x + 5, self.height() // 2)
        painter.end()

    def mousePressEvent(self, event):
        if event.button() == Qt.LeftButton:
            if abs(event.x() - int(self.width() * self.slider_position)) < 20:
                self.dragging = False
                self._move_slider = True
            else:
                self._move_slider = False
                self.dragging = True
                self.last_pos = event.pos()

    def mouseMoveEvent(self, event):
        if getattr(self, '_move_slider', False):
            self.setSliderPosition(event.x() / self.width())
        elif self.dragging and self.last_pos:
            self.pan_offset += (event.pos() - self.last_pos)
            self.last_pos = event.pos()
            self.update()

    def mouseReleaseEvent(self, event):
        self.dragging = False
        self._move_slider = False


class AudioWaveWidget(QWidget):

    def __init__(self, parent=None):
        super().__init__(parent)
        self.before_env = None
        self.after_env = None
        self.colors = PALETTE_WAVE_COLORS['electric']
        self.setMinimumSize(400, 300)
        self.setStyleSheet(f"background-color: {THEME['surface']}; border: 1px solid {THEME['border']}; border-radius: 6px;")

    def setAudio(self, before_env, after_env, palette):
        self.before_env = before_env
        self.after_env = after_env
        self.colors = PALETTE_WAVE_COLORS.get(palette, PALETTE_WAVE_COLORS['electric'])
        self.update()

    def _bar_color(self, v):
        c0, c1, c2 = self.colors
        if v < 0.7:
            t = v / 0.7
            return tuple(int(c0[i] + (c1[i] - c0[i]) * t) for i in range(3))
        t = (v - 0.7) / 0.3
        return tuple(int(c1[i] + (c2[i] - c1[i]) * t) for i in range(3))

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.fillRect(self.rect(), QColor(THEME['surface']))
        w, h = self.width(), self.height()
        cy = h // 2
        painter.setPen(QPen(QColor(THEME['border']), 1))
        painter.drawLine(0, cy, w, cy)
        env = self.after_env
        if env is None or len(env) == 0:
            painter.setPen(QPen(QColor(THEME['text_secondary'])))
            painter.drawText(self.rect(), Qt.AlignCenter, "No audio preview")
            painter.end()
            return
        n = len(env)
        bar_w = w / n
        if self.before_env is not None and len(self.before_env) == n:
            painter.setPen(Qt.NoPen)
            painter.setBrush(QColor(84, 84, 92, 110))
            for i, v in enumerate(self.before_env):
                bh = max(1, int(v * h * 0.40))
                painter.drawRect(QRect(int(i * bar_w), cy - bh, max(1, int(bar_w) - 1), 2 * bh))
        painter.setPen(Qt.NoPen)
        for i, v in enumerate(env):
            bh = max(1, int(v * h * 0.44))
            painter.setBrush(QColor(*self._bar_color(float(v))))
            painter.drawRect(QRect(int(i * bar_w), cy - bh, max(1, int(bar_w) - 1), 2 * bh))
        painter.end()


class ProcessingThread(QThread):
    progress_update = pyqtSignal(int, str)
    processing_complete = pyqtSignal(list, bool)
    file_done = pyqtSignal(str, str, str)

    def __init__(self, commands, cwd, inputs=None):
        super().__init__()
        self.commands = commands
        self.cwd = cwd
        self.inputs = inputs if inputs is not None else [''] * len(commands)
        self.cancelled = False
        self.process = None

    def run(self):
        outputs = []
        self.arrow_output = ''
        total = len(self.commands)
        try:
            for idx, command in enumerate(self.commands):
                if self.cancelled:
                    break
                self.process = subprocess.Popen(
                    command,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    bufsize=1,
                    cwd=self.cwd
                )
                last_percent = 0
                while True:
                    if self.cancelled:
                        self.process.terminate()
                        break
                    line = self.process.stdout.readline()
                    if not line and self.process.poll() is not None:
                        break
                    line = line.strip()
                    if not line:
                        continue
                    if line.startswith('{') and line.endswith('}'):
                        try:
                            data = json.loads(line)
                            percent = data.get('percent', 0)
                            step = data.get('step', '')
                            overall = int((idx + percent / 100.0) / total * 100)
                            self.progress_update.emit(min(overall, 99), step)
                            last_percent = percent
                        except json.JSONDecodeError:
                            pass
                    else:
                        if '\u2192' in line and ('results' in line or line.rsplit('  ', 1)[-1].strip()):
                            self.arrow_output = line.split('\u2192', 1)[1].split('(', 1)[0].strip()
                        if '%' in line:
                            import re
                            match = re.search(r'(\d+)%', line)
                            if match:
                                percent = int(match.group(1))
                                overall = int((idx + percent / 100.0) / total * 100)
                                self.progress_update.emit(min(overall, 99), line[-60:])
                if self.cancelled:
                    self.processing_complete.emit([], False)
                    return
                if self.process.returncode != 0:
                    self.processing_complete.emit(outputs, False)
                    return
                output_path = self.arrow_output or self._extract_output(command)
                self.arrow_output = ''
                if output_path:
                    outputs.append(output_path)
                    self.file_done.emit(self.inputs[idx], output_path, "done")
            self.progress_update.emit(100, "Complete")
            self.processing_complete.emit(outputs, True)
        except Exception as e:
            self.processing_complete.emit(outputs, False)

    def _extract_output(self, command):
        try:
            i = command.index('-o')
            return command[i + 1]
        except (ValueError, IndexError):
            return ''

    def cancel(self):
        self.cancelled = True
        process = self.process
        if process is not None and process.poll() is None:
            process.terminate()
IMAGE_EXTS = {'.jpg', '.jpeg', '.png', '.bmp', '.tiff', '.tif', '.webp'}
VIDEO_EXTS = {'.mp4', '.avi', '.mov', '.mkv', '.webm', '.flv', '.wmv', '.m4v'}
AUDIO_EXTS = {'.mp3', '.wav', '.flac', '.ogg', '.m4a', '.aac', '.wma', '.opus'}
MESH_EXTS = {'.obj', '.stl', '.ply'}
SUPPORTED_EXTS = IMAGE_EXTS | VIDEO_EXTS | AUDIO_EXTS | MESH_EXTS
PALETTE_NAMES = ['electric', 'crimson', 'ice', 'toxic', 'violet', 'golden', 'ghost']
PALETTE_WAVE_COLORS = {
    'electric': ((14, 54, 200), (70, 190, 255), (240, 252, 255)),
    'crimson': ((180, 16, 42), (255, 70, 120), (255, 240, 244)),
    'ice': ((36, 74, 142), (140, 205, 245), (255, 255, 255)),
    'toxic': ((18, 110, 28), (110, 240, 60), (228, 255, 214)),
    'violet': ((90, 18, 180), (190, 80, 255), (244, 236, 255)),
    'golden': ((170, 90, 10), (250, 190, 60), (255, 248, 214)),
    'ghost': ((86, 86, 94), (198, 201, 208), (255, 255, 255)),
}
AUDIO_PROFILE_INFO = [
    ('slash', 'the signature diagonal energy sweep'),
    ('fire', 'burn — crackle, rumble, flicker, heat drive'),
    ('ice', 'ice-steam — octave shimmer, airy shelf, glassy breath'),
    ('robotic', 'metal ring-mod, formant combs, crushed edges'),
    ('ghost', 'fog — breathing dark reverb, whisper detune'),
    ('void', 'the abyss — octave-down, huge dark space'),
    ('echo', 'proper clean ping-pong echo, tone-shaped'),
]


def engine_module():
    try:
        base_dir = os.path.dirname(os.path.abspath(__file__))
        if base_dir not in sys.path:
            sys.path.insert(0, base_dir)
        import neonify
        return neonify
    except Exception:
        return None


def audio_envelope(path, points=1000):
    try:
        import numpy as np
        cmd = ['ffmpeg', '-y', '-loglevel', 'error', '-i', str(path),
               '-vn', '-ac', '1', '-ar', '8000', '-f', 's16le', 'pipe:1']
        result = subprocess.run(cmd, capture_output=True, timeout=600)
        if result.returncode != 0 or len(result.stdout) < 2048:
            return None
        x = np.frombuffer(result.stdout, dtype='<i2').astype(np.float32) / 32768.0
        if len(x) < points:
            return None
        buckets = np.array_split(x, points)
        env = np.array([float(np.sqrt(np.mean(b ** 2))) if len(b) else 0.0 for b in buckets], dtype=np.float32)
        peak = max(float(np.percentile(env, 99.0)), 1e-6)
        return np.clip(env / peak, 0.0, 1.0)
    except Exception:
        return None


def detect_command(path):
    ext = Path(path).suffix.lower()
    if ext in IMAGE_EXTS:
        return 'image'
    if ext in VIDEO_EXTS:
        return 'video'
    if ext in AUDIO_EXTS:
        return 'audio'
    if ext in MESH_EXTS:
        return 'mesh'
    return None


def default_output_for(path, command, turntable=0):
    p = Path(path)
    stamp = datetime.datetime.now().strftime("_%y%m%d%H%M%S")
    if command == 'audio':
        return str(p.parent / f"{p.stem}_neon{stamp}.wav")
    if command == 'mesh':
        if turntable > 0:
            return str(p.parent / f"{p.stem}_neon_turntable{stamp}.mp4")
        return str(p.parent / f"{p.stem}_neon{stamp}.png")
    return str(p.parent / f"{p.stem}_neon{stamp}{p.suffix}")


def script_base():
    if getattr(sys, 'frozen', False):
        return [sys.executable], os.path.dirname(os.path.abspath(sys.executable))
    script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "neonify.py")
    return [sys.executable, script], os.path.dirname(script)


class FileList(QListWidget):

    files_added = pyqtSignal()
    duplicates_skipped = pyqtSignal(int)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setAcceptDrops(True)
        self.setStyleSheet(get_list_style())
        self.setDragEnabled(False)
        self.setSelectionMode(QListWidget.ExtendedSelection)

    def dragEnterEvent(self, event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dragMoveEvent(self, event):
        if event.mimeData().hasUrls():
            event.acceptProposedAction()

    def dropEvent(self, event):
        paths = []
        for url in event.mimeData().urls():
            p = url.toLocalFile()
            if p and Path(p).suffix.lower() in SUPPORTED_EXTS:
                paths.append(p)
        added = dup = 0
        for p in paths:
            if not self._contains(p):
                self.addItem(p)
                added += 1
            else:
                dup += 1
        if added:
            self.files_added.emit()
        if dup:
            self.duplicates_skipped.emit(dup)
        event.acceptProposedAction()

    def _contains(self, path):
        return any(self.item(i).text() == path for i in range(self.count()))

    def addPaths(self, paths):
        added = dup = 0
        for p in paths:
            if Path(p).suffix.lower() not in SUPPORTED_EXTS:
                continue
            if self._contains(p):
                dup += 1
                continue
            self.addItem(p)
            added += 1
        if added:
            self.files_added.emit()
        if dup:
            self.duplicates_skipped.emit(dup)
        return added

    def allPaths(self):
        return [self.item(i).text() for i in range(self.count())]


class AdvancedAudioDialog(QDialog):

    def __init__(self, profile, parent=None):
        super().__init__(parent)
        self.setWindowTitle("Advanced audio settings")
        self.setMinimumWidth(480)
        self.setStyleSheet(
            f"QDialog {{ background-color: {THEME['panel_background']}; }}"
            f"QLabel {{ color: {THEME['text']}; background: transparent; }}"
            f"QDoubleSpinBox {{ background-color: {THEME['surface']}; color: {THEME['text']};"
            f"border: 1px solid {THEME['border']}; border-radius: 4px; padding: 4px; }}")
        self.advanced = None
        eng = engine_module()
        self.schema = dict(getattr(eng, 'AUDIO_ADVANCED_SCHEMA', {}) or {})
        lay = QVBoxLayout(self)
        prow = QHBoxLayout()
        plabel = QLabel("Profile")
        self.profile_combo = QComboBox()
        for name, desc in AUDIO_PROFILE_INFO:
            self.profile_combo.addItem(f"{name} — {desc}", name)
        idx = self.profile_combo.findData(profile)
        if idx >= 0:
            self.profile_combo.setCurrentIndex(idx)
        self.profile_combo.currentIndexChanged.connect(self._rebuild)
        prow.addWidget(plabel)
        prow.addWidget(self.profile_combo, 1)
        lay.addLayout(prow)
        self.hint = QLabel("")
        self.hint.setWordWrap(True)
        self.hint.setStyleSheet(f"color: {THEME['text_secondary']}; font-size: 11px;")
        lay.addWidget(self.hint)
        self.rows = QWidget()
        self.rows_lay = QVBoxLayout(self.rows)
        self.rows_lay.setContentsMargins(0, 0, 0, 0)
        lay.addWidget(self.rows)
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self._accept)
        buttons.rejected.connect(self.reject)
        lay.addWidget(buttons)
        self._rebuild()

    def _rebuild(self):
        profile = self.profile_combo.currentData()
        while self.rows_lay.count():
            item = self.rows_lay.takeAt(0)
            w = item.widget()
            if w is not None:
                w.deleteLater()
        self.spins = {}
        schema = self.schema.get(profile, [])
        if not schema:
            self.hint.setText("Advanced parameters are not available for this profile (engine schema missing).")
            return
        self.hint.setText("Overrides the profile's tuned defaults — leave untouched to keep the stock sound.")
        for key, desc, lo, hi, dflt in schema:
            row = QHBoxLayout()
            lab = QLabel(desc)
            spin = QDoubleSpinBox()
            spin.setRange(lo, hi)
            spin.setDecimals(2)
            spin.setSingleStep(max((hi - lo) / 100.0, 0.01))
            spin.setValue(dflt)
            row.addWidget(lab, 1)
            row.addWidget(spin)
            self.rows_lay.addLayout(row)
            self.spins[key] = spin

    def _accept(self):
        self.advanced = {k: round(s.value(), 3) for k, s in self.spins.items()}
        self.accept()


class NeonifyGUI(QMainWindow):

    def __init__(self):
        super().__init__()
        self.processor = None
        self.last_input = None
        self.last_output = None
        self.last_palette = 'electric'
        self.temp_preview = None
        self.media_player = None
        if HAS_MULTIMEDIA:
            self.media_player = QMediaPlayer()
            self.media_player.stateChanged.connect(self._on_player_state)
            self.media_player.error.connect(self._on_media_error)
        self.setWindowTitle(f"NEONIFY — Procedural Neon Art Tool")
        self.setWindowIcon(load_app_icon())
        self.setStyleSheet(f"QMainWindow {{ background-color: {THEME['background']}; }}")
        self.resize(1280, 800)
        self.setMinimumSize(1080, 680)
        self._build_ui()

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(14, 10, 14, 10)
        root.setSpacing(10)

        header = QHBoxLayout()
        logo_label = QLabel()
        icon_path = get_icon_path()
        if os.path.exists(icon_path):
            logo_label.setPixmap(QPixmap(icon_path).scaled(44, 44, Qt.KeepAspectRatio, Qt.SmoothTransformation))
        title_box = QVBoxLayout()
        title = QLabel("NEONIFY")
        title.setStyleSheet(f"color: {THEME['text']}; font-size: 22px; font-weight: bold; letter-spacing: 4px;")
        title_box.addWidget(title)
        header.addWidget(logo_label)
        header.addLayout(title_box)
        header.addStretch()
        version = QLabel("v0.5.0")
        version.setStyleSheet(f"color: {THEME['text_secondary']}; font-size: 12px; padding-right: 4px;")
        header.addWidget(version)
        root.addLayout(header)

        body = QHBoxLayout()
        left = QVBoxLayout()
        drop_panel = QFrame()
        drop_panel.setStyleSheet(get_panel_style())
        drop_layout = QVBoxLayout(drop_panel)
        drop_title = QLabel("INPUT FILES")
        drop_title.setStyleSheet(f"color: {THEME['text']}; font-size: 12px; font-weight: bold; border: none;")
        drop_layout.addWidget(drop_title)
        self.file_list = FileList()
        self.file_list.files_added.connect(self._refresh_states)
        self.file_list.duplicates_skipped.connect(self._warn_duplicates)
        self.file_list.itemSelectionChanged.connect(self._preview_selected_input)
        drop_layout.addWidget(self.file_list, 1)
        btn_row = QHBoxLayout()
        add_btn = QPushButton("Add Files")
        add_btn.setStyleSheet(get_secondary_button_style())
        add_btn.clicked.connect(self._add_files)
        clear_btn = QPushButton("Clear")
        clear_btn.setStyleSheet(get_secondary_button_style())
        clear_btn.clicked.connect(self._clear_files)
        btn_row.addWidget(add_btn)
        btn_row.addWidget(clear_btn)
        btn_row.addStretch()
        drop_layout.addLayout(btn_row)
        hint = QLabel("Drop images, videos, audio or meshes (.obj / .stl)")
        hint.setStyleSheet(f"color: {THEME['text_secondary']}; font-size: 11px; border: none;")
        hint.setWordWrap(True)
        drop_layout.addWidget(hint)
        left.addWidget(drop_panel, 3)

        settings_panel = QFrame()
        settings_panel.setStyleSheet(get_panel_style())
        settings = QVBoxLayout(settings_panel)
        settings_title = QLabel("SETTINGS")
        settings_title.setStyleSheet(f"color: {THEME['text']}; font-size: 12px; font-weight: bold; border: none;")
        settings.addWidget(settings_title)

        mode_row = QHBoxLayout()
        mode_label = QLabel("Mode")
        mode_label.setStyleSheet(f"color: {THEME['text_secondary']}; font-size: 12px; border: none;")
        self.mode_combo = QComboBox()
        self.mode_combo.addItems(["Auto-detect", "Neon (images/videos)", "Audio", "Mesh"])
        self.mode_combo.setStyleSheet(get_combo_box_style())
        mode_row.addWidget(mode_label)
        mode_row.addWidget(self.mode_combo, 1)
        settings.addLayout(mode_row)

        palette_row = QHBoxLayout()
        palette_label = QLabel("Palette")
        palette_label.setStyleSheet(f"color: {THEME['text_secondary']}; font-size: 12px; border: none;")
        self.palette_combo = QComboBox()
        for name in PALETTE_NAMES:
            self.palette_combo.addItem(name.capitalize(), name)
        self.palette_combo.setStyleSheet(get_combo_box_style())
        palette_row.addWidget(palette_label)
        palette_row.addWidget(self.palette_combo, 1)
        settings.addLayout(palette_row)

        glow_box = QVBoxLayout()
        glow_header = QHBoxLayout()
        glow_label = QLabel("Glow")
        glow_label.setStyleSheet(f"color: {THEME['text_secondary']}; font-size: 12px; border: none;")
        self.glow_value = QLabel("1.00")
        self.glow_value.setStyleSheet(f"color: {THEME['text']}; font-size: 12px; border: none;")
        glow_header.addWidget(glow_label)
        glow_header.addStretch()
        glow_header.addWidget(self.glow_value)
        self.glow_slider = QSlider(Qt.Horizontal)
        self.glow_slider.setRange(20, 240)
        self.glow_slider.setValue(100)
        self.glow_slider.setStyleSheet(get_slider_style())
        self.glow_slider.valueChanged.connect(lambda v: self.glow_value.setText(f"{v / 100.0:.2f}"))
        glow_box.addLayout(glow_header)
        glow_box.addWidget(self.glow_slider)
        settings.addLayout(glow_box)

        thr_box = QVBoxLayout()
        thr_header = QHBoxLayout()
        thr_label = QLabel("Edge threshold")
        thr_label.setStyleSheet(f"color: {THEME['text_secondary']}; font-size: 12px; border: none;")
        self.thr_value = QLabel("0.12")
        self.thr_value.setStyleSheet(f"color: {THEME['text']}; font-size: 12px; border: none;")
        thr_header.addWidget(thr_label)
        thr_header.addStretch()
        thr_header.addWidget(self.thr_value)
        self.thr_slider = QSlider(Qt.Horizontal)
        self.thr_slider.setRange(2, 50)
        self.thr_slider.setValue(12)
        self.thr_slider.setStyleSheet(get_slider_style())
        self.thr_slider.valueChanged.connect(lambda v: self.thr_value.setText(f"{v / 100.0:.2f}"))
        thr_box.addLayout(thr_header)
        thr_box.addWidget(self.thr_slider)
        settings.addLayout(thr_box)

        self.turntable_check = QCheckBox("Mesh → turntable orbit video (MP4)")
        self.turntable_check.setStyleSheet(get_checkbox_style())
        settings.addWidget(self.turntable_check)

        audio_row = QHBoxLayout()
        audio_label = QLabel("Audio profile")
        audio_label.setStyleSheet(f"color: {THEME['text_secondary']}; font-size: 12px; border: none;")
        self.profile_combo = QComboBox()
        for name, desc in AUDIO_PROFILE_INFO:
            self.profile_combo.addItem(f"{name} — {desc}", name)
        self.profile_combo.setStyleSheet(get_combo_box_style())
        audio_row.addWidget(audio_label)
        audio_row.addWidget(self.profile_combo, 1)
        settings.addLayout(audio_row)

        self.neon_audio_check = QCheckBox("Neonify audio with video")
        self.neon_audio_check.setStyleSheet(get_checkbox_style())
        settings.addWidget(self.neon_audio_check)

        self.spatial_check = QCheckBox("Spatial glow (stereo pan)")
        self.spatial_check.setStyleSheet(get_checkbox_style())
        self.spatial_check.setChecked(True)
        settings.addWidget(self.spatial_check)

        self.keep_inside_check = QCheckBox("Keep the inside (original look inside the edges)")
        self.keep_inside_check.setStyleSheet(get_checkbox_style())
        settings.addWidget(self.keep_inside_check)

        self.hwaccel_check = QCheckBox("ffmpeg hwaccel decode (optional)")
        self.hwaccel_check.setStyleSheet(get_checkbox_style())
        settings.addWidget(self.hwaccel_check)

        self.next_to_input_check = QCheckBox("Save next to input instead of results/")
        self.next_to_input_check.setStyleSheet(get_checkbox_style())
        settings.addWidget(self.next_to_input_check)

        self.adv_audio_btn = QPushButton("Advanced audio settings…")
        self.adv_audio_btn.setStyleSheet(get_secondary_button_style())
        self.adv_audio_btn.clicked.connect(self._open_advanced_audio)
        settings.addWidget(self.adv_audio_btn)

        self.process_btn = QPushButton("NEONIFY")
        self.process_btn.setStyleSheet(get_accent_button_style())
        self.process_btn.clicked.connect(self._process)
        settings.addWidget(self.process_btn)

        self.open_folder_btn = QPushButton("Open Output Folder")
        self.open_folder_btn.setStyleSheet(get_secondary_button_style())
        self.open_folder_btn.setEnabled(False)
        self.open_folder_btn.clicked.connect(self._open_folder)
        settings.addWidget(self.open_folder_btn)

        self.progress_bar = QProgressBar()
        self.progress_bar.setStyleSheet(get_progress_bar_style())
        self.progress_bar.setValue(0)
        settings.addWidget(self.progress_bar)

        self.status_label = QLabel("Ready — drop files to begin")
        self.status_label.setStyleSheet(f"color: {THEME['text_secondary']}; font-size: 11px; border: none;")
        self.status_label.setWordWrap(True)
        settings.addWidget(self.status_label)
        settings.addStretch()
        left.addWidget(settings_panel, 2)

        right = QVBoxLayout()
        preview_panel = QFrame()
        preview_panel.setStyleSheet(get_panel_style())
        preview_layout = QVBoxLayout(preview_panel)
        preview_header = QHBoxLayout()
        preview_title = QLabel("PREVIEW")
        preview_title.setStyleSheet(f"color: {THEME['text']}; font-size: 12px; font-weight: bold; border: none;")
        self.preview_mode_btn = QPushButton("Before / After")
        self.preview_mode_btn.setStyleSheet(get_surface_button_style())
        self.preview_mode_btn.setEnabled(False)
        self.preview_mode_btn.clicked.connect(self._toggle_preview_mode)
        self.play_btn = QPushButton("Play")
        self.play_btn.setStyleSheet(get_surface_button_style())
        self.play_btn.setEnabled(False)
        self.play_btn.setVisible(False)
        self.play_btn.clicked.connect(self._toggle_play)
        preview_header.addWidget(preview_title)
        preview_header.addStretch()
        self.clear_preview_btn = QPushButton("Clear previews")
        self.clear_preview_btn.setStyleSheet(get_surface_button_style())
        self.clear_preview_btn.clicked.connect(self._clear_previews)
        preview_header.addWidget(self.play_btn)
        preview_header.addWidget(self.preview_mode_btn)
        preview_header.addWidget(self.clear_preview_btn)
        preview_layout.addLayout(preview_header)

        self.preview_stack = QStackedWidget()
        self.comparison = ImageComparisonSlider()
        self.viewer = ScrollableImageViewer()
        self.audio_wave = AudioWaveWidget()
        self.preview_stack.addWidget(self.comparison)
        self.preview_stack.addWidget(self.viewer)
        self.preview_stack.addWidget(self.audio_wave)
        preview_layout.addWidget(self.preview_stack, 1)

        self.preview_caption = QLabel("Results appear here after processing")
        self.preview_caption.setAlignment(Qt.AlignCenter)
        self.preview_caption.setStyleSheet(f"color: {THEME['text_secondary']}; font-size: 11px; border: none;")
        preview_layout.addWidget(self.preview_caption)
        right.addWidget(preview_panel, 1)

        body.addLayout(left, 1)
        body.addLayout(right, 1)
        root.addLayout(body, 1)

    def _add_files(self):
        exts = []
        for e in sorted(SUPPORTED_EXTS):
            exts.append(f"*{e}")
        files, _ = QFileDialog.getOpenFileNames(
            self, "Select media files", "",
            f"Media files ({' '.join(exts)});;All files (*.*)"
        )
        if files:
            self.file_list.addPaths(files)

    def _clear_files(self):
        self.file_list.clear()
        self._refresh_states()

    def _warn_duplicates(self, n):
        self.status_label.setText(f"{n} file(s) already in the queue — skipped")

    def _preview_selected_input(self):
        items = self.file_list.selectedItems()
        if not items:
            return
        path = items[0].text()
        ext = Path(path).suffix.lower()
        if HAS_MULTIMEDIA:
            self.media_player.stop()
        if ext in {'.jpg', '.jpeg', '.png', '.bmp', '.webp', '.tif', '.tiff'}:
            pm = QPixmap(path)
            if not pm.isNull():
                self.preview_stack.setCurrentWidget(self.viewer)
                self.viewer.setPixmap(pm)
                self.preview_caption.setText(f"input — {os.path.basename(path)}")
                self.play_btn.setVisible(False)
        elif ext in {'.mp4', '.avi', '.mkv', '.mov', '.webm', '.gif'}:
            if not HAS_MULTIMEDIA:
                self.preview_stack.setCurrentWidget(self.viewer)
                self.viewer.show_text(f"video input — {os.path.basename(path)}\n(QtMultimedia unavailable)")
                return
            self.media_player.setMedia(QMediaContent(QUrl.fromLocalFile(path)))
            self.preview_stack.setCurrentWidget(self.viewer)
            self.viewer.show_text(f"video input — {os.path.basename(path)}\npress Play to preview")
            self.play_btn.setVisible(True)
            self.play_btn.setEnabled(True)
            self.preview_caption.setText(f"video input — {os.path.basename(path)}")
        elif ext in {'.wav', '.mp3', '.flac', '.ogg', '.m4a', '.aac', '.wma'}:
            if not HAS_MULTIMEDIA:
                self.preview_stack.setCurrentWidget(self.viewer)
                self.viewer.show_text(f"audio input — {os.path.basename(path)}\n(QtMultimedia unavailable)")
                return
            self.media_player.setMedia(QMediaContent(QUrl.fromLocalFile(path)))
            self.preview_stack.setCurrentWidget(self.audio_wave)
            self.play_btn.setVisible(True)
            self.play_btn.setEnabled(True)
            self.preview_caption.setText(f"audio input — {os.path.basename(path)}")
        else:
            self.preview_stack.setCurrentWidget(self.viewer)
            self.viewer.show_text(f"3D input — {os.path.basename(path)}\nrenders on process")
            self.play_btn.setVisible(False)
            self.preview_caption.setText(f"3D input — {os.path.basename(path)}")

    def _refresh_states(self):
        count = len(self.file_list.allPaths())
        if count == 0:
            self.status_label.setText("Ready — drop files to begin")
        else:
            self.status_label.setText(f"{count} file(s) queued")

    def _mode_for(self, path):
        mode_idx = self.mode_combo.currentIndex()
        detected = detect_command(path)
        if mode_idx == 0:
            return detected
        if mode_idx == 1:
            return detected if detected in ('image', 'video') else None
        if mode_idx == 2:
            return 'audio'
        return 'mesh'

    def _open_advanced_audio(self):
        dlg = AdvancedAudioDialog(self.profile_combo.currentData(), self)
        if dlg.exec_() == QDialog.Accepted and dlg.advanced is not None:
            self.advanced_json = json.dumps(dlg.advanced)
            self.adv_audio_btn.setText("Advanced audio: on")

    def _advanced_json(self):
        return getattr(self, 'advanced_json', None)

    def _process(self):
        paths = self.file_list.allPaths()
        if not paths:
            QMessageBox.warning(self, "NEONIFY", "Add or drop at least one supported file first.")
            return
        base, cwd = script_base()
        commands = []
        inputs = []
        for p in paths:
            command = self._mode_for(p)
            if command is None:
                continue
            turntable = 120 if (self.turntable_check.isChecked() and command == 'mesh') else 0
            args = list(base) + [
                command, p,
                '--json-progress',
                '--palette', self.palette_combo.currentData(),
                '--glow', f"{self.glow_slider.value() / 100.0:g}",
                '--threshold', f"{self.thr_slider.value() / 100.0:g}",
            ]
            if command in ('audio', 'video'):
                args.extend(['--profile', self.profile_combo.currentData()])
                adv = self._advanced_json()
                if adv:
                    args.extend(['--advanced-audio', adv])
            if command == 'video':
                if self.neon_audio_check.isChecked():
                    args.append('--neon-audio')
                if not self.spatial_check.isChecked():
                    args.append('--no-spatial')
                if self.hwaccel_check.isChecked():
                    args.append('--hwaccel')
            if command in ('image', 'video') and self.keep_inside_check.isChecked():
                args.append('--keep-inside')
            if self.next_to_input_check.isChecked():
                args.append('--next-to-input')
            if turntable > 0:
                args.extend(['--turntable', str(turntable)])
            commands.append(args)
            inputs.append(p)
        if not commands:
            QMessageBox.warning(self, "NEONIFY", "No files matched the selected mode.")
            return
        self.process_btn.setEnabled(False)
        self.preview_mode_btn.setEnabled(False)
        self.last_palette = self.palette_combo.currentData()
        self.progress_bar.setValue(0)
        self.status_label.setText(f"Processing {len(commands)} file(s)...")
        self.processor = ProcessingThread(commands, cwd, inputs)
        self.processor.progress_update.connect(self._on_progress)
        self.processor.file_done.connect(self._on_file_done)
        self.processor.processing_complete.connect(self._on_complete)
        self.processor.start()

    def _on_progress(self, percent, step):
        self.progress_bar.setValue(percent)
        display = step if len(step) < 70 else step[:67] + "..."
        self.status_label.setText(display)

    def _clear_previews(self):
        if self.media_player is not None:
            self.media_player.stop()
        self.last_input = None
        self.last_output = None
        self.temp_preview = None
        self.preview_stack.setCurrentIndex(1)
        self.viewer.setPixmap(QPixmap())
        self.preview_caption.setText("Results appear here after processing")
        self.status_label.setText("")

    def _on_file_done(self, input_path, output_path, status):
        self.last_output = output_path
        if input_path:
            self.last_input = input_path

    def _on_complete(self, outputs, success):
        self.process_btn.setEnabled(True)
        self.processor = None
        if success and outputs:
            self.progress_bar.setValue(100)
            self.status_label.setText(f"Done — {len(outputs)} file(s) generated\n" + "\n".join(outputs[-3:]))
            self.open_folder_btn.setEnabled(True)
            self.preview_output(outputs[-1])
        elif success:
            self.status_label.setText("Cancelled")
        else:
            self.progress_bar.setValue(0)
            self.status_label.setText("Processing failed — see the console output for details")
            QMessageBox.warning(self, "NEONIFY", "Processing failed.\nCheck that ffmpeg is installed for video/audio inputs.")

    def _extract_video_frame(self, video_path, position=0.5):
        try:
            import cv2
            cap = cv2.VideoCapture(video_path)
            if not cap.isOpened():
                return None
            total = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
            target = int(total * position)
            cap.set(cv2.CAP_PROP_POS_FRAMES, max(0, target))
            ret, frame = cap.read()
            cap.release()
            if not ret:
                return None
            rgb = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            h, w, ch = rgb.shape
            img = QImage(rgb.data, w, h, ch * w, QImage.Format_RGB888)
            self.temp_preview = img.copy()
            return QPixmap.fromImage(img)
        except Exception:
            return None

    def preview_output(self, output_path):
        p = Path(output_path)
        if not p.exists():
            return
        ext = p.suffix.lower()
        after_pixmap = None
        before_pixmap = None
        if ext in AUDIO_EXTS:
            if self.media_player is not None:
                self.media_player.stop()
                self.media_player.setMedia(QMediaContent(QUrl.fromLocalFile(str(p))))
            after_env = audio_envelope(str(p))
            before_env = audio_envelope(self.last_input) if self.last_input and Path(self.last_input).exists() else None
            self.audio_wave.setAudio(before_env, after_env, self.last_palette)
            self.preview_stack.setCurrentWidget(self.audio_wave)
            self.preview_mode_btn.setEnabled(False)
            self.play_btn.setVisible(True)
            self.play_btn.setEnabled(HAS_MULTIMEDIA)
            self.preview_caption.setText(f"Audio preview — {p.name}" + ("  (gray: source, neon: result)" if before_env is not None else ""))
            return
        self.play_btn.setVisible(False)
        if self.media_player is not None:
            self.media_player.stop()
        if ext in VIDEO_EXTS:
            after_pixmap = self._extract_video_frame(str(p))
            if self.last_input and Path(self.last_input).exists() and Path(self.last_input).suffix.lower() in VIDEO_EXTS:
                before_pixmap = self._extract_video_frame(self.last_input)
            self.preview_caption.setText(f"Video preview frame — {p.name}")
        else:
            pm = QPixmap(str(p))
            if not pm.isNull():
                after_pixmap = pm
            if self.last_input and Path(self.last_input).exists() and Path(self.last_input).suffix.lower() in IMAGE_EXTS:
                before_pixmap = QPixmap(self.last_input)
            self.preview_caption.setText(f"{p.name}")
        if after_pixmap is None:
            return
        if before_pixmap is not None and not before_pixmap.isNull():
            self.comparison.setBeforeImage(before_pixmap)
            self.comparison.setAfterImage(after_pixmap)
            self.comparison.setSliderPosition(0.5)
            self.comparison.resetView()
            self.preview_stack.setCurrentWidget(self.comparison)
            self.preview_mode_btn.setEnabled(True)
            self._showing_comparison = True
            self.preview_mode_btn.setText("Result only")
        else:
            self.viewer.setPixmap(after_pixmap)
            self.preview_stack.setCurrentWidget(self.viewer)
            self.preview_mode_btn.setEnabled(False)

    def _toggle_preview_mode(self):
        showing_comparison = self.preview_stack.currentWidget() is self.comparison
        if showing_comparison:
            self.preview_stack.setCurrentWidget(self.viewer)
            if self.comparison.after_pixmap:
                self.viewer.setPixmap(self.comparison.after_pixmap)
            self.preview_mode_btn.setText("Before / After")
        else:
            self.preview_stack.setCurrentWidget(self.comparison)
            self.preview_mode_btn.setText("Result only")

    def _toggle_play(self):
        if self.media_player is None:
            return
        if self.media_player.state() == QMediaPlayer.PlayingState:
            self.media_player.pause()
        else:
            self.media_player.play()

    def _on_player_state(self, state):
        if state == QMediaPlayer.PlayingState:
            self.play_btn.setText("Pause")
        else:
            self.play_btn.setText("Play")

    def _on_media_error(self, *args):
        self.play_btn.setEnabled(False)
        self.preview_caption.setText(self.preview_caption.text() + "  — playback unavailable on this system")

    def _open_folder(self):
        if not self.last_output:
            return
        folder = str(Path(self.last_output).parent)
        from PyQt5.QtCore import QUrl
        from PyQt5.QtGui import QDesktopServices
        QDesktopServices.openUrl(QUrl.fromLocalFile(folder))

    def closeEvent(self, event):
        if self.media_player is not None:
            self.media_player.stop()
        if self.processor is not None and self.processor.isRunning():
            self.processor.cancel()
            self.processor.wait(3000)
        event.accept()


def main():
    if hasattr(Qt, 'AA_EnableHighDpiScaling'):
        QApplication.setAttribute(Qt.AA_EnableHighDpiScaling, True)
    if hasattr(Qt, 'AA_UseHighDpiPixmaps'):
        QApplication.setAttribute(Qt.AA_UseHighDpiPixmaps, True)
    app = QApplication(sys.argv)
    app.setWindowIcon(load_app_icon())
    app.setStyle("Fusion")
    window = NeonifyGUI()
    window.show()
    sys.exit(app.exec_())


if __name__ == '__main__':
    main()
