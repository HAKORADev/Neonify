import os
import sys

os.environ.setdefault("KMP_DUPLICATE_LIB_OK", "TRUE")

for _stream in (sys.stdout, sys.stderr):
    if hasattr(_stream, "reconfigure"):
        _stream.reconfigure(encoding="utf-8", errors="replace")

import argparse
import json
import math
import re
import shutil
import struct
import subprocess
import threading
import time
from pathlib import Path

import numpy as np
import torch
import torch.nn.functional as F
import cv2
from tqdm import tqdm

import warnings
warnings.filterwarnings("ignore")

try:
    import pyfiglet
    HAS_PYFIGLET = True
except ImportError:
    HAS_PYFIGLET = False

VERSION = "0.5.0"
JSON_PROGRESS = False

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
TEMP_DIR = os.path.join(SCRIPT_DIR, "tmp")

IMAGE_EXTENSIONS = {'.jpg', '.jpeg', '.png', '.bmp', '.tiff', '.tif', '.webp'}
VIDEO_EXTENSIONS = {'.mp4', '.avi', '.mov', '.mkv', '.webm', '.flv', '.wmv', '.m4v'}
AUDIO_EXTENSIONS = {'.mp3', '.wav', '.flac', '.ogg', '.m4a', '.aac', '.wma', '.opus'}
MESH_EXTENSIONS = {'.obj', '.stl'}

PALETTE_NAMES = ['electric', 'synthwave', 'toxic', 'ice', 'fire', 'ghost', 'spectrum']

device = None

_BANNER_FONT = {
    'N': ["10001", "11001", "10101", "10011", "10001"],
    'E': ["11111", "10000", "11110", "10000", "11111"],
    'O': ["01110", "10001", "10001", "10001", "01110"],
    'I': ["11111", "00100", "00100", "00100", "11111"],
    'F': ["11111", "10000", "11110", "10000", "10000"],
    'Y': ["10001", "10001", "01010", "00100", "00100"],
}


def print_banner():
    title = "NEONIFY"
    if HAS_PYFIGLET:
        lines = pyfiglet.figlet_format(title, font="big").rstrip("\n").split("\n")
    else:
        lines = []
        for row in range(5):
            parts = []
            for ch in title:
                cell = _BANNER_FONT.get(ch, ["00000"] * 5)
                parts.append(cell[row].replace("0", ".").replace("1", "█"))
            lines.append("  ".join(parts))
    width = max(len(line) for line in lines)
    mid = width // 2
    blue = "\033[38;5;39m"
    red = "\033[38;5;203m"
    white = "\033[38;5;255m"
    reset = "\033[0m"
    print()
    for line in lines:
        left = line[:mid]
        right = line[mid:]
        colored = blue + left + reset
        if mid > 0 and len(left) > 0 and line[mid - 1] not in (" ",):
            pass
        colored += red + right + reset
        print(colored)
    print(f"{white}              procedural neon engine v{VERSION}{reset}")
    print("=" * 60)


class ProgressTracker:
    def __init__(self):
        self.total_files = 0
        self.current_file_idx = 0
        self.start_time = None
        self.current_file_start = None
        self.current_step = ""
        self.current_file_name = ""
        self.file_times = []
        self._last_update = 0

    def start_batch(self, total_files):
        self.total_files = total_files
        self.current_file_idx = 0
        self.start_time = time.time()
        self.file_times = []

    def start_file(self, file_name):
        self.current_file_idx += 1
        self.current_file_name = os.path.basename(file_name)
        self.current_file_start = time.time()
        self.current_step = "Loading"

    def set_step(self, step_name):
        self.current_step = step_name

    def finish_file(self):
        if self.current_file_start:
            elapsed = time.time() - self.current_file_start
            self.file_times.append(elapsed)
            return elapsed
        return 0

    def get_elapsed_str(self):
        if not self.start_time:
            return "00:00"
        elapsed = time.time() - self.start_time
        return self._format_time(elapsed)

    def get_eta_str(self):
        if len(self.file_times) < 2:
            return "calculating..."
        avg_time = sum(self.file_times) / len(self.file_times)
        remaining_files = self.total_files - self.current_file_idx
        eta_seconds = avg_time * remaining_files
        return self._format_time(eta_seconds)

    def _format_time(self, seconds):
        if seconds < 3600:
            return time.strftime("%M:%S", time.gmtime(seconds))
        return time.strftime("%H:%M:%S", time.gmtime(seconds))

    def print_status(self, force=False):
        now = time.time()
        if not force and (now - self._last_update) < 0.1:
            return
        self._last_update = now
        elapsed = self.get_elapsed_str()
        eta = self.get_eta_str()
        progress_pct = (self.current_file_idx / self.total_files * 100) if self.total_files > 0 else 0
        display_name = self.current_file_name
        if len(display_name) > 35:
            display_name = display_name[:32] + "..."

        if JSON_PROGRESS:
            json_output = json.dumps({
                'percent': int(progress_pct),
                'step': self.current_step,
                'file': display_name,
                'file_num': self.current_file_idx,
                'total_files': self.total_files,
                'elapsed': elapsed,
                'eta': eta
            })
            print(json_output)
            sys.stdout.flush()
        else:
            status = f"\r[{self.current_file_idx}/{self.total_files}] ({progress_pct:5.1f}%) | {elapsed} elapsed, ETA: {eta} | {display_name} | {self.current_step}"
            status = status.ljust(120)
            sys.stdout.write(status)
            sys.stdout.flush()

    def print_newline(self):
        sys.stdout.write("\n")
        sys.stdout.flush()


progress = ProgressTracker()


class StepProgressBar:
    def __init__(self, steps, file_name=""):
        self.steps = steps
        self.current_step_idx = 0
        self.file_name = file_name
        self.bar_width = 30

    def update(self, step_name):
        self.current_step_idx += 1
        progress_pct = self.current_step_idx / self.steps * 100
        filled = int(self.bar_width * self.current_step_idx / self.steps)
        bar = "█" * filled + "░" * (self.bar_width - filled)
        display_name = self.file_name
        if len(display_name) > 25:
            display_name = display_name[:22] + "..."
        status = f"\r  {display_name} [{bar}] {self.current_step_idx}/{self.steps} ({progress_pct:5.1f}%) - {step_name}"
        status = status.ljust(100)
        sys.stdout.write(status)
        sys.stdout.flush()

    def finish(self):
        bar = "█" * self.bar_width
        status = f"\r  {self.file_name[:25]:<25} [{bar}] {self.steps}/{self.steps} (100.0%) - Done!"
        status = status.ljust(100)
        sys.stdout.write(status)
        sys.stdout.flush()
        print()


class _SilentStepBar:
    def update(self, msg):
        pass


def check_gpu():
    if torch.cuda.is_available():
        gpu_name = torch.cuda.get_device_name(0)
        gpu_memory = torch.cuda.get_device_properties(0).total_memory / (1024**3)
        return True, gpu_name, f"{gpu_memory:.1f}GB"
    return False, "CPU", "N/A"


def get_device(force_cpu=False, device_type=None):
    global device
    if device is not None:
        return device
    if force_cpu or device_type == 'cpu':
        device = torch.device('cpu')
        print("Device: CPU")
        return device
    if device_type == 'gpu':
        if torch.cuda.is_available():
            device = torch.device('cuda')
            print(f"Device: GPU ({torch.cuda.get_device_name(0)})")
        else:
            print("GPU requested but not available - falling back to CPU")
            device = torch.device('cpu')
        return device
    if torch.cuda.is_available():
        device = torch.device('cuda')
        print(f"Device: GPU ({torch.cuda.get_device_name(0)})")
    else:
        device = torch.device('cpu')
        print("Device: CPU (no GPU available)")
    return device


def select_device():
    global device
    has_gpu, gpu_name, gpu_memory = check_gpu()
    print("\n" + "-"*40)
    print("Select device:")
    if has_gpu:
        print(f"  1. CPU")
        print(f"  2. GPU ({gpu_name}, {gpu_memory})")
    else:
        print(f"  1. CPU (only option available)")
        print(f"  2. GPU (not available)")
    print("\n  Press Enter for auto-detect (GPU if available, else CPU)")
    choice = input("> ").strip()
    if choice == '':
        if has_gpu:
            device = torch.device('cuda')
            print(f"\nUsing GPU: {gpu_name} ({gpu_memory})")
        else:
            device = torch.device('cpu')
            print("\nUsing CPU (no GPU available)")
    elif choice == '1':
        device = torch.device('cpu')
        print("\nUsing CPU")
    elif choice == '2':
        if has_gpu:
            device = torch.device('cuda')
            print(f"\nUsing GPU: {gpu_name} ({gpu_memory})")
        else:
            print("\nGPU requested but not available - falling back to CPU")
            device = torch.device('cpu')
    else:
        print("\nInvalid choice, auto-detecting...")
        if has_gpu:
            device = torch.device('cuda')
            print(f"Using GPU: {gpu_name} ({gpu_memory})")
        else:
            device = torch.device('cpu')
            print("Using CPU")
    return device


def ensure_ffmpeg():
    if shutil.which('ffmpeg') is None:
        raise RuntimeError("ffmpeg not found. Please install ffmpeg to process videos and audio.")


_blur_kernel_cache = {}


def _gaussian_kernel1d(sigma):
    radius = max(1, int(math.ceil(sigma * 3.0)))
    size = 2 * radius + 1
    x = torch.arange(size, dtype=torch.float32) - radius
    k = torch.exp(-(x * x) / (2.0 * sigma * sigma))
    return k / k.sum()


def gaussian_blur(t, sigma):
    if sigma <= 0.05:
        return t
    key = (str(t.device), round(sigma, 3))
    if key not in _blur_kernel_cache:
        k = _gaussian_kernel1d(sigma).to(t.device)
        _blur_kernel_cache[key] = k
    k = _blur_kernel_cache[key]
    channels = t.shape[1]
    kx = k.view(1, 1, 1, -1).repeat(channels, 1, 1, 1)
    ky = k.view(1, 1, -1, 1).repeat(channels, 1, 1, 1)
    t = F.conv2d(t, kx, padding=(0, kx.shape[-1] // 2), groups=channels)
    t = F.conv2d(t, ky, padding=(ky.shape[-2] // 2, 0), groups=channels)
    return t


_SOBEL_KX = torch.tensor([[-1.0, 0.0, 1.0], [-2.0, 0.0, 2.0], [-1.0, 0.0, 1.0]]) / 4.0
_SOBEL_KY = _SOBEL_KX.T.contiguous()


def luminance(t):
    r, g, b = t[:, 0:1, :, :], t[:, 1:2, :, :], t[:, 2:3, :, :]
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def sobel_field(t):
    kx = _SOBEL_KX.view(1, 1, 3, 3).to(t.device)
    ky = _SOBEL_KY.view(1, 1, 3, 3).to(t.device)
    gx = F.conv2d(t, kx, padding=1)
    gy = F.conv2d(t, ky, padding=1)
    mag = torch.sqrt(gx * gx + gy * gy + 1e-8)
    ang = torch.atan2(gy, gx)
    return mag, ang


def smoothstep(lo, hi, x):
    t = torch.clamp((x - lo) / (hi - lo + 1e-8), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def hsv_to_rgb(h, s, v):
    h = (h % 1.0) * 6.0
    i = torch.floor(h).long() % 6
    f = h - torch.floor(h)
    p = v * (1.0 - s)
    q = v * (1.0 - s * f)
    t = v * (1.0 - s * (1.0 - f))
    r = torch.where(i == 0, v, torch.where(i == 1, q, torch.where(i == 2, p, torch.where(i == 3, p, torch.where(i == 4, t, v)))))
    g = torch.where(i == 0, t, torch.where(i == 1, v, torch.where(i == 2, v, torch.where(i == 3, q, torch.where(i == 4, p, p)))))
    b = torch.where(i == 0, p, torch.where(i == 1, p, torch.where(i == 2, t, torch.where(i == 3, v, torch.where(i == 4, v, q)))))
    return torch.stack([r, g, b], dim=1)


def build_palette_lut(name):
    stops = {
        'electric': [(0.00, (0.004, 0.010, 0.045)), (0.28, (0.05, 0.25, 0.85)), (0.58, (0.15, 0.72, 1.00)), (0.84, (0.60, 0.95, 1.00)), (1.00, (1.00, 1.00, 1.00))],
        'synthwave': [(0.00, (0.040, 0.004, 0.060)), (0.30, (0.45, 0.08, 0.75)), (0.60, (0.98, 0.20, 0.60)), (0.85, (1.00, 0.50, 0.45)), (1.00, (1.00, 0.96, 0.88))],
        'toxic': [(0.00, (0.004, 0.035, 0.012)), (0.30, (0.05, 0.50, 0.12)), (0.64, (0.35, 0.95, 0.20)), (1.00, (0.90, 1.00, 0.80))],
        'ice': [(0.00, (0.004, 0.012, 0.024)), (0.35, (0.10, 0.30, 0.55)), (0.70, (0.55, 0.80, 0.95)), (1.00, (1.00, 1.00, 1.00))],
        'fire': [(0.00, (0.035, 0.005, 0.002)), (0.30, (0.55, 0.08, 0.02)), (0.64, (1.00, 0.45, 0.05)), (0.88, (1.00, 0.80, 0.30)), (1.00, (1.00, 1.00, 0.92))],
        'ghost': [(0.00, (0.012, 0.012, 0.014)), (0.40, (0.35, 0.35, 0.38)), (0.75, (0.75, 0.78, 0.82)), (1.00, (1.00, 1.00, 1.00))],
    }
    if name == 'spectrum':
        idx = np.linspace(0.0, 1.0, 256)
        h = torch.from_numpy(idx).float().view(1, -1)
        rgb = hsv_to_rgb(h, torch.full_like(h, 0.9), torch.ones_like(h))
        lut = rgb.squeeze(0).permute(1, 0).numpy()
    else:
        rows = stops[name]
        px = np.array([r[0] for r in rows])
        cols = np.array([r[1] for r in rows], dtype=np.float32)
        idx = np.linspace(0.0, 1.0, 256)
        lut = np.stack([np.interp(idx, px, cols[:, c]) for c in range(3)], axis=1)
    return lut.astype(np.float32)
def to_tensor(img_bgr):
    rgb = cv2.cvtColor(img_bgr, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
    return torch.from_numpy(rgb).permute(2, 0, 1).unsqueeze(0).to(get_device())


def to_img(t):
    t = t.squeeze(0).permute(1, 2, 0).detach().cpu().numpy()
    t = np.clip(t * 255.0, 0, 255).astype(np.uint8)
    return cv2.cvtColor(t, cv2.COLOR_RGB2BGR)


_GLOW_SIGMAS = (2.0, 6.0, 16.0, 38.0)
_GLOW_WEIGHTS = (0.9, 0.62, 0.45, 0.32)


def wide_blur(t, sigma):
    if sigma >= 12.0:
        small = F.interpolate(t, scale_factor=0.25, mode='bilinear', align_corners=False)
        small = gaussian_blur(small, sigma * 0.25)
        return F.interpolate(small, size=t.shape[-2:], mode='bilinear', align_corners=False)
    return gaussian_blur(t, sigma)


def neonize_edges(t, lut_t, glow=1.0, threshold=0.12, env=1.0, hue_mix=0.0):
    gray = luminance(t)
    base = gaussian_blur(gray, 1.0)
    mag, ang = sobel_field(base)
    edge = smoothstep(threshold, threshold + 0.22, mag)
    glow_field = torch.zeros_like(edge)
    for sigma, weight in zip(_GLOW_SIGMAS, _GLOW_WEIGHTS):
        glow_field = glow_field + wide_blur(edge, sigma) * (weight * glow * env)
    excite = 0.72 + 0.42 * env
    energy = torch.clamp((edge * 1.15 + glow_field * 0.85) * excite, 0.0, 1.0)
    idx = torch.clamp((energy * 255.0).long(), 0, 255)
    color = lut_t[idx].permute(0, 4, 1, 2, 3).squeeze(2)
    if hue_mix > 0.0:
        hue = ((ang + math.pi) / (2.0 * math.pi)) % 1.0
        hcol = hsv_to_rgb(hue, torch.full_like(hue, 0.9), energy)
        hcol = hcol.reshape(1, 3, hue.shape[-2], hue.shape[-1])
        color = color * (1.0 - hue_mix) + hcol * hue_mix
    core = torch.pow(edge, 1.2) * 0.85
    color = color + core * 0.55
    return torch.clamp(color, 0.0, 1.0)


_BLOOM_SIGMAS = (3.0, 10.0, 28.0, 64.0)
_BLOOM_WEIGHTS = (0.85, 0.55, 0.35, 0.28)


def bloom_color(t, glow=1.0, env=1.0):
    out = t.clone()
    for sigma, weight in zip(_BLOOM_SIGMAS, _BLOOM_WEIGHTS):
        out = out + wide_blur(t, sigma) * (weight * glow * env)
    lum = luminance(t)
    core = torch.pow(smoothstep(0.25, 0.9, lum), 1.1) * 0.35
    out = out + core
    return torch.clamp(out, 0.0, 1.0)


def process_image_neon(img, step_bar=None, palette='electric', glow=1.0, threshold=0.12, env=1.0):
    if step_bar:
        step_bar.update("Detecting edges...")
    else:
        progress.set_step("Neonizing")
        progress.print_status()
    lut = torch.from_numpy(build_palette_lut(palette)).to(get_device())
    t = to_tensor(img)
    hue_mix = 1.0 if palette == 'spectrum' else 0.0
    result = neonize_edges(t, lut, glow=glow, threshold=threshold, env=env, hue_mix=hue_mix)
    if step_bar:
        step_bar.update("Blooming...")
    return to_img(result)


def _probe_ffmpeg_option(args):
    try:
        cmd = ['ffmpeg', '-hide_banner', '-loglevel', 'error',
               '-f', 'lavfi', '-i', 'color=c=black:s=64x64:r=25:d=0.04',
               '-frames:v', '1', '-f', 'null', '-'] + args
        return subprocess.run(cmd, capture_output=True, timeout=60).returncode == 0
    except Exception:
        return False


def _ffmpeg_version_tuple():
    try:
        out = subprocess.run(['ffmpeg', '-version'], capture_output=True, text=True, timeout=60).stdout
        match = re.search(r'ffmpeg version (\d+)\.(\d+)', out)
        if match:
            return int(match.group(1)), int(match.group(2))
    except Exception:
        pass
    return 0, 0


_vsync_args_cache = None


def vsync_args():
    global _vsync_args_cache
    if _vsync_args_cache is None:
        if _probe_ffmpeg_option(['-fps_mode', 'passthrough']):
            _vsync_args_cache = ['-fps_mode', 'passthrough']
        elif _probe_ffmpeg_option(['-vsync', '0']):
            _vsync_args_cache = ['-vsync', '0']
        else:
            _vsync_args_cache = ['-fps_mode', 'passthrough'] if _ffmpeg_version_tuple() >= (5, 1) else ['-vsync', '0']
    return list(_vsync_args_cache)


def extract_frames(video_path, output_dir, desc="Extracting frames"):
    os.makedirs(output_dir, exist_ok=True)
    cap = cv2.VideoCapture(video_path)
    total_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    cap.release()
    cmd = [
        'ffmpeg', '-y', '-loglevel', 'error', '-i', video_path,
        *vsync_args(),
        os.path.join(output_dir, '%08d.png')
    ]
    process = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    with tqdm(total=total_frames, desc=desc, unit="frames",
              bar_format="{l_bar}{bar:30}{r_bar}") as pbar:
        while process.poll() is None:
            if os.path.exists(output_dir):
                current_frames = len([f for f in os.listdir(output_dir) if f.endswith('.png')])
                pbar.update(current_frames - pbar.n)
            time.sleep(0.1)
        if os.path.exists(output_dir):
            current_frames = len([f for f in os.listdir(output_dir) if f.endswith('.png')])
            pbar.update(current_frames - pbar.n)
    if process.returncode != 0:
        raise RuntimeError(f"ffmpeg frame extraction failed with exit code {process.returncode}")


def extract_audio(video_path, audio_path):
    cmd = [
        'ffmpeg', '-y', '-i', video_path,
        '-vn', '-acodec', 'copy',
        audio_path
    ]
    result = subprocess.run(cmd, capture_output=True)
    return result.returncode == 0


def get_video_info(video_path):
    cap = cv2.VideoCapture(video_path)
    fps = cap.get(cv2.CAP_PROP_FPS)
    frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))
    width = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    height = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    cap.release()
    return fps, frame_count, width, height


def frames_to_video(frames_dir, output_path, fps, audio_path=None, desc="Compiling video"):
    frames = sorted([f for f in os.listdir(frames_dir) if f.endswith('.png')])
    total_frames = len(frames)
    temp_video = output_path + '_temp.mp4'
    cmd = [
        'ffmpeg', '-y',
        '-framerate', str(fps),
        '-i', os.path.join(frames_dir, '%08d.png'),
        '-c:v', 'libx264',
        '-pix_fmt', 'yuv420p',
        '-crf', '18',
        '-progress', 'pipe:2',
        temp_video
    ]
    compiled_frames = [0]
    process = subprocess.Popen(cmd, stderr=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=1)

    def _read_ffmpeg_progress(pipe):
        for raw_line in iter(pipe.readline, b''):
            decoded = raw_line.decode('utf-8', errors='ignore').strip()
            if decoded.startswith('frame='):
                try:
                    compiled_frames[0] = int(decoded.split('=')[1].strip())
                except (ValueError, IndexError):
                    pass
        pipe.close()

    reader = threading.Thread(target=_read_ffmpeg_progress, args=(process.stderr,), daemon=True)
    reader.start()

    last_reported = [0]
    with tqdm(total=total_frames, desc=desc, unit="frames",
              bar_format="{desc} | {n_fmt}/{total_fmt} frames {bar:25} {percentage:5.1f}% | {elapsed}<{remaining}, {rate_fmt}{postfix}") as pbar:
        while process.poll() is None:
            time.sleep(0.05)
            current = compiled_frames[0]
            if current > last_reported[0]:
                pbar.update(current - last_reported[0])
                last_reported[0] = current
                remaining = total_frames - current
                pbar.set_postfix_str(f"{remaining} remaining")
        reader.join(timeout=2)
        final = compiled_frames[0]
        if final > last_reported[0]:
            pbar.update(final - last_reported[0])
            last_reported[0] = final
        fill_needed = max(0, total_frames - pbar.n)
        if fill_needed > 0:
            pbar.update(fill_needed)
        pbar.set_postfix_str("")
    if audio_path and os.path.exists(audio_path):
        cmd = [
            'ffmpeg', '-y',
            '-i', temp_video,
            '-i', audio_path,
            '-c:v', 'copy',
            '-c:a', 'aac',
            '-map', '0:v:0',
            '-map', '1:a:0?',
            output_path
        ]
        result = subprocess.run(cmd, capture_output=True)
        if result.returncode == 0 and os.path.exists(output_path):
            os.remove(temp_video)
            return
    if os.path.exists(temp_video):
        if os.path.exists(output_path):
            os.remove(output_path)
        os.rename(temp_video, output_path)


def decode_audio_pcm(path, sr=22050):
    ensure_ffmpeg()
    cmd = [
        'ffmpeg', '-y', '-loglevel', 'error', '-i', path,
        '-vn', '-ac', '1', '-ar', str(sr), '-f', 's16le', 'pipe:1'
    ]
    result = subprocess.run(cmd, capture_output=True, timeout=600)
    if result.returncode != 0 or len(result.stdout) < 1024:
        return None, sr
    x = np.frombuffer(result.stdout, dtype=np.int16).astype(np.float32) / 32768.0
    return x, sr


def audio_envelope_for_frames(audio_path, n_frames, fps):
    x, sr = decode_audio_pcm(audio_path)
    if x is None or len(x) < sr // 4:
        return None
    hop = 512
    n_hops = len(x) // hop
    if n_hops < 2:
        return None
    trimmed = x[:n_hops * hop].reshape(n_hops, hop)
    rms = np.sqrt((trimmed ** 2).mean(axis=1) + 1e-9)
    hop_t = np.arange(n_hops) * hop / sr
    frame_t = np.arange(n_frames) / max(fps, 1e-6)
    env = np.interp(frame_t, hop_t, rms)
    kernel = max(1, int(fps / 6))
    env = np.convolve(env, np.ones(kernel) / kernel, mode='same')
    peak = max(np.percentile(env, 92), 1e-6)
    env = np.clip(env / peak, 0.0, 1.6)
    return 0.55 + 0.55 * env


def stft_magnitude(x, n_fft=2048, hop=512):
    window = np.hanning(n_fft).astype(np.float32)
    n_frames = 1 + max(0, (len(x) - n_fft) // hop)
    if n_frames < 2:
        return None
    idx = np.arange(n_fft)[None, :] + hop * np.arange(n_frames)[:, None]
    frames = x[idx] * window[None, :]
    return np.abs(np.fft.rfft(frames, axis=1))


def _resize_width(matrix, target_w):
    h, w = matrix.shape
    if w <= target_w:
        return matrix
    return cv2.resize(matrix, (target_w, h), interpolation=cv2.INTER_LINEAR)


def neonize_audio_file(input_path, output_path, opts, step_bar=None):
    x, sr = decode_audio_pcm(input_path)
    if x is None:
        raise RuntimeError(f"Could not decode audio from: {input_path}")
    if step_bar:
        step_bar.update("Analyzing spectrum...")
    mag = stft_magnitude(x, n_fft=2048, hop=512)
    if mag is None:
        raise RuntimeError("Audio too short to analyze")
    logm = np.log1p(mag * 4.0)
    peak = max(np.percentile(logm, 99.6), 1e-6)
    energy = np.clip(logm / peak, 0.0, 1.0)
    n_bins = energy.shape[1]
    rows = 512
    fmin, fmax = 2, n_bins - 1
    row_bins = np.geomspace(fmin, fmax, rows)
    b0 = np.floor(row_bins).astype(int)
    b1 = np.clip(b0 + 1, 0, n_bins - 1)
    w1 = row_bins - b0
    spec = energy[:, b0] * (1.0 - w1[None, :]) + energy[:, b1] * w1[None, :]
    spec = spec.T[::-1]
    target_w = min(spec.shape[1], 2400)
    spec = _resize_width(spec, target_w)
    lut = torch.from_numpy(build_palette_lut(opts.palette)).to(get_device())
    energy_t = torch.from_numpy(spec.astype(np.float32))[None, None].to(get_device())
    idx = torch.clamp((energy_t * 255.0).long(), 0, 255)
    color = lut[idx].permute(0, 4, 1, 2, 3).squeeze(2)
    emag, _ = sobel_field(gaussian_blur(energy_t, 1.0))
    contour = smoothstep(0.10, 0.32, emag) * 0.5
    color = color + contour * 0.4
    color = bloom_color(color, glow=opts.glow * 0.9, env=1.0)
    img = to_img(color)
    cv2.imwrite(output_path, img)


def neonize_audio_anim(input_path, output_path, opts, step_bar=None):
    x, sr = decode_audio_pcm(input_path, sr=22050)
    if x is None:
        raise RuntimeError(f"Could not decode audio from: {input_path}")
    fps = 30
    hop = int(sr / fps)
    n_fft = 4096
    mag = stft_magnitude(x, n_fft=n_fft, hop=hop)
    if mag is None:
        raise RuntimeError("Audio too short to analyze")
    total_frames = mag.shape[0]
    n_bins = mag.shape[1]
    band_edges = np.unique(np.geomspace(2, n_bins - 1, 65).astype(int))
    bands = len(band_edges) - 1
    band_values = np.stack([mag[:, band_edges[i]:band_edges[i + 1]].mean(axis=1) for i in range(bands)], axis=1)
    logb = np.log1p(band_values * 3.0)
    peak = max(np.percentile(logb, 99.0), 1e-6)
    band_norm = np.clip(logb / peak, 0.0, 1.0)
    lut_u8 = (build_palette_lut(opts.palette) * 255.0).clip(0, 255).astype(np.uint8)
    width, height = 1280, 720
    margin_x = 40
    bar_w = (width - 2 * margin_x) / bands
    cy = height // 2
    frames_dir = os.path.join(TEMP_DIR, "audio_anim")
    if os.path.exists(TEMP_DIR):
        shutil.rmtree(TEMP_DIR)
    os.makedirs(frames_dir, exist_ok=True)
    try:
        for i in tqdm(range(total_frames), desc="Neonizing audio", unit="frame",
                      bar_format="{l_bar}{bar:30}{r_bar}"):
            canvas = np.zeros((height, width, 3), dtype=np.uint8)
            grad = np.linspace(0.0, 1.0, height).reshape(-1, 1, 1)
            base_top = np.array([8, 6, 14], dtype=np.float32)
            base_bottom = np.array([2, 2, 4], dtype=np.float32)
            canvas += (base_bottom * (1.0 - grad) + base_top * grad).astype(np.uint8)
            cv2.line(canvas, (margin_x, cy), (width - margin_x, cy), (36, 30, 46), 1, cv2.LINE_AA)
            for b in range(bands):
                v = float(band_norm[i, b])
                if v < 0.015:
                    continue
                bh = int((v ** 1.15) * (height * 0.44))
                if bh < 2:
                    continue
                x0 = int(margin_x + b * bar_w + bar_w * 0.18)
                x1 = int(margin_x + (b + 1) * bar_w - bar_w * 0.18)
                color = lut_u8[int(v * 255)][::-1].tolist()
                cv2.rectangle(canvas, (x0, cy - bh), (x1, cy + bh), tuple(color), -1)
                cap_color = (255, 255, 255) if v > 0.82 else tuple(min(255, c + 70) for c in color)
                cv2.rectangle(canvas, (x0, cy - bh - 3), (x1, cy - bh), tuple(cap_color), -1)
                cv2.rectangle(canvas, (x0, cy + bh), (x1, cy + bh + 3), tuple(cap_color), -1)
            t = to_tensor(canvas)
            out_t = bloom_color(t, glow=opts.glow, env=1.0)
            frame = to_img(out_t)
            cv2.imwrite(os.path.join(frames_dir, f'{i:08d}.png'), frame)
        frames_to_video(frames_dir, output_path, fps, audio_path=input_path, desc="Compiling video")
    finally:
        if os.path.exists(TEMP_DIR):
            shutil.rmtree(TEMP_DIR, ignore_errors=True)


def parse_obj_mesh(path):
    verts = []
    faces = []
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            if line.startswith('v '):
                parts = line.split()
                verts.append([float(parts[1]), float(parts[2]), float(parts[3])])
            elif line.startswith('f '):
                parts = line.split()[1:]
                idxs = []
                for p in parts:
                    token = p.split('/')[0]
                    if not token:
                        continue
                    i = int(token)
                    if i < 0:
                        i = len(verts) + i
                    else:
                        i = i - 1
                    idxs.append(i)
                if len(idxs) >= 2:
                    faces.append(idxs)
    edges = set()
    for idxs in faces:
        n = len(idxs)
        for k in range(n):
            a, b = idxs[k], idxs[(k + 1) % n]
            if a != b:
                edges.add((min(a, b), max(a, b)))
    return np.asarray(verts, dtype=np.float32), np.asarray(sorted(edges), dtype=np.int64)


def parse_stl_mesh(path):
    with open(path, 'rb') as f:
        f.read(80)
        count_bytes = f.read(4)
        if len(count_bytes) < 4:
            raise RuntimeError(f"Invalid STL file: {path}")
        count = struct.unpack('<I', count_bytes)[0]
        expected = 84 + count * 50
        actual = os.path.getsize(path)
        if expected == actual:
            tris = []
            for _ in range(count):
                data = f.read(50)
                vals = struct.unpack('<12fH', data)
                tris.append([vals[3:6], vals[6:9], vals[9:12]])
            verts = np.asarray(tris, dtype=np.float32).reshape(-1, 3)
        else:
            f.seek(0)
            text = f.read().decode('utf-8', errors='ignore')
            tokens = re.findall(r'vertex\s+([-\d.eE+]+)\s+([-\d.eE+]+)\s+([-\d.eE+]+)', text)
            if len(tokens) < 3:
                raise RuntimeError(f"Invalid STL file: {path}")
            verts = np.asarray(tokens, dtype=np.float32)
    n = len(verts) // 3
    verts = verts[:n * 3]
    unique_verts, inverse = np.unique(np.round(verts, 6), axis=0, return_inverse=True)
    inverse = inverse.reshape(n, 3)
    edge_set = set()
    for tri in inverse:
        for k in range(3):
            a, b = int(tri[k]), int(tri[(k + 1) % 3])
            if a != b:
                edge_set.add((min(a, b), max(a, b)))
    return unique_verts.astype(np.float32), np.asarray(sorted(edge_set), dtype=np.int64)


def load_mesh_edges(path):
    ext = Path(path).suffix.lower()
    if ext == '.obj':
        return parse_obj_mesh(path)
    if ext == '.stl':
        return parse_stl_mesh(path)
    raise RuntimeError(f"Unsupported mesh format: {ext}")


def _rotation_matrix(az_deg, el_deg):
    az = math.radians(az_deg)
    el = math.radians(el_deg)
    ca, sa = math.cos(az), math.sin(az)
    ce, se = math.cos(el), math.sin(el)
    ry = np.array([[ca, 0.0, sa], [0.0, 1.0, 0.0], [-sa, 0.0, ca]], dtype=np.float32)
    rx = np.array([[1.0, 0.0, 0.0], [0.0, ce, -se], [0.0, se, ce]], dtype=np.float32)
    return rx @ ry


def _mesh_density(n_edges):
    return float(np.clip(1500.0 / max(n_edges, 1), 0.08, 1.0))


def render_wire_view(V, E, size, az_deg, el_deg, dist, palette, lw):
    center = (V.max(axis=0) + V.min(axis=0)) / 2.0
    span = max(float(np.max(np.abs(V - center))) * 2.0, 1e-6)
    pts = (V - center) / span * 1.7
    pts = pts @ _rotation_matrix(az_deg, el_deg).T
    f = 1.8
    z = pts[:, 2] + dist
    z = np.maximum(z, 0.15)
    px = pts[:, 0] * f / z
    py = pts[:, 1] * f / z
    half = size / 2.0
    xs = (px * half * 0.62 + half).astype(np.int32)
    ys = (-py * half * 0.62 + half).astype(np.int32)
    edge_z = z[E].mean(axis=1)
    zmin, zmax = float(edge_z.min()), float(edge_z.max())
    nearness = np.clip((zmax - edge_z) / max(zmax - zmin, 1e-6), 0.0, 1.0)
    nearness = 0.18 + 0.82 * np.power(nearness, 1.1)
    density = _mesh_density(len(E))
    color_scale = 1.0 if density >= 1.0 else (0.22 + 0.78 * density)
    lut_u8 = (build_palette_lut(palette) * 255.0).clip(0, 255).astype(np.uint8)
    canvas = np.zeros((size, size, 3), dtype=np.uint8)
    order = np.argsort(-edge_z)
    lw = 1 if len(E) > 2500 else max(1, int(size / 640))
    for ei in order:
        a, b = E[ei]
        color = lut_u8[int(nearness[ei] * 255)].astype(np.float32) * color_scale
        color = tuple(np.clip(color, 0, 255).astype(np.uint8)[::-1].tolist())
        cv2.line(canvas, (int(xs[a]), int(ys[a])), (int(xs[b]), int(ys[b])), color, lw, cv2.LINE_AA)
    return canvas


def neonize_mesh_file(input_path, output_path, opts, turntable=0):
    V, E = load_mesh_edges(input_path)
    if len(V) == 0 or len(E) == 0:
        raise RuntimeError(f"No geometry found in: {input_path}")
    density = _mesh_density(len(E))
    if turntable > 0:
        frames_dir = os.path.join(TEMP_DIR, "mesh_turntable")
        if os.path.exists(TEMP_DIR):
            shutil.rmtree(TEMP_DIR)
        os.makedirs(frames_dir, exist_ok=True)
        try:
            for i in tqdm(range(turntable), desc="Rendering turntable", unit="frame",
                          bar_format="{l_bar}{bar:30}{r_bar}"):
                az = opts.azimuth + (360.0 * i / turntable)
                canvas = render_wire_view(V, E, 1024, az, opts.elevation, 3.4, opts.palette, 2)
                t = to_tensor(canvas)
                out_t = bloom_color(t, glow=opts.glow, env=1.0)
                emag, _ = sobel_field(luminance(gaussian_blur(t, 1.0)))
                contour = smoothstep(0.08, 0.28, emag) * 0.45 * density
                out_t = torch.clamp(out_t + contour * 0.5, 0.0, 1.0)
                frame = to_img(out_t)
                cv2.imwrite(os.path.join(frames_dir, f'{i:08d}.png'), frame)
            frames_to_video(frames_dir, output_path, 30, audio_path=None, desc="Compiling video")
        finally:
            if os.path.exists(TEMP_DIR):
                shutil.rmtree(TEMP_DIR, ignore_errors=True)
    else:
        canvas = render_wire_view(V, E, 1024, opts.azimuth, opts.elevation, 3.4, opts.palette, 2)
        t = to_tensor(canvas)
        out_t = bloom_color(t, glow=opts.glow, env=1.0)
        emag, _ = sobel_field(luminance(gaussian_blur(t, 1.0)))
        contour = smoothstep(0.08, 0.28, emag) * 0.45 * density
        out_t = torch.clamp(out_t + contour * 0.5, 0.0, 1.0)
        img = to_img(out_t)
        cv2.imwrite(output_path, img)
def is_image(path):
    return Path(path).suffix.lower() in IMAGE_EXTENSIONS


def is_video(path):
    return Path(path).suffix.lower() in VIDEO_EXTENSIONS


def is_audio(path):
    return Path(path).suffix.lower() in AUDIO_EXTENSIONS


def is_mesh(path):
    return Path(path).suffix.lower() in MESH_EXTENSIONS


def get_files(path):
    path = Path(path)
    if path.is_file():
        return [str(path)]
    elif path.is_dir():
        files = []
        for ext in IMAGE_EXTENSIONS | VIDEO_EXTENSIONS | AUDIO_EXTENSIONS | MESH_EXTENSIONS:
            files.extend(path.glob(f"*{ext}"))
            files.extend(path.glob(f"*{ext.upper()}"))
        return sorted([str(f) for f in files])
    return []


def parse_multiple_paths(input_string):
    paths = []
    current = ""
    in_quotes = False
    quote_char = None
    i = 0
    while i < len(input_string):
        char = input_string[i]
        if char in ['"', "'"]:
            if not in_quotes:
                in_quotes = True
                quote_char = char
            elif char == quote_char:
                in_quotes = False
                quote_char = None
            else:
                current += char
        elif char in [' ', '\t'] and not in_quotes:
            if current.strip():
                paths.append(current.strip())
            current = ""
        else:
            current += char
        i += 1
    if current.strip():
        paths.append(current.strip())
    return paths


def categorize_path(path_str):
    path = Path(path_str)
    if not path.exists():
        cleaned = path_str.strip()
        if len(cleaned) == 0:
            return ('invalid', [])
        has_alnum = any(c.isalnum() for c in cleaned)
        if not has_alnum:
            return ('invalid', [])
        return ('not_exist', [])
    if path.is_file():
        if is_image(str(path)) or is_video(str(path)) or is_audio(str(path)) or is_mesh(str(path)):
            return ('valid', [str(path)])
        else:
            return ('not_supported', [])
    elif path.is_dir():
        files = get_files(str(path))
        if files:
            return ('valid', files)
        else:
            return ('not_supported', [])
    else:
        return ('not_supported', [])


def categorize_multiple_paths(path_list):
    result = {
        'valid': [],
        'not_exist': [],
        'not_supported': [],
        'invalid': [],
        'all_valid_files': []
    }
    for path_str in path_list:
        category, valid_files = categorize_path(path_str)
        if category == 'valid':
            result['valid'].append((path_str, valid_files))
            result['all_valid_files'].extend(valid_files)
        elif category == 'not_exist':
            result['not_exist'].append(path_str)
        elif category == 'not_supported':
            result['not_supported'].append(path_str)
        else:
            result['invalid'].append(path_str)
    return result


def display_path_summary(categorized, max_display=5):
    valid = categorized['valid']
    not_exist = categorized['not_exist']
    not_supported = categorized['not_supported']
    invalid = categorized['invalid']
    total_paths = len(valid) + len(not_exist) + len(not_supported) + len(invalid)
    if total_paths == 0:
        return
    print("\n" + "-"*60)
    print("INPUT PATH SUMMARY")
    print("-"*60)
    if valid:
        print(f"\n✓ VALID ({len(valid)}):")
        display_count = min(len(valid), max_display)
        for i in range(display_count):
            orig_path, files = valid[i]
            if len(files) == 1:
                print(f"   {orig_path}")
            else:
                print(f"   {orig_path}/ ({len(files)} files)")
        if len(valid) > max_display:
            remaining = len(valid) - max_display
            print(f"   ... +{remaining} more valid")
    if not_exist:
        print(f"\n✗ NOT FOUND ({len(not_exist)}):")
        display_count = min(len(not_exist), max_display)
        for i in range(display_count):
            print(f"   {not_exist[i]}")
        if len(not_exist) > max_display:
            remaining = len(not_exist) - max_display
            print(f"   ... +{remaining} more not found")
    if not_supported:
        print(f"\n⚠ NOT SUPPORTED ({len(not_supported)}):")
        display_count = min(len(not_supported), max_display)
        for i in range(display_count):
            print(f"   {not_supported[i]}")
        if len(not_supported) > max_display:
            remaining = len(not_supported) - max_display
            print(f"   ... +{remaining} more not supported")
    if invalid:
        print(f"\n? INVALID INPUT ({len(invalid)}):")
        display_count = min(len(invalid), max_display)
        for i in range(display_count):
            print(f"   {invalid[i]}")
        if len(invalid) > max_display:
            remaining = len(invalid) - max_display
            print(f"   ... +{remaining} more invalid")
    valid_file_count = len(categorized['all_valid_files'])
    if valid_file_count > 0:
        print(f"\n→ {valid_file_count} file(s) ready to process from {len(valid)} valid path(s)")
    else:
        print(f"\n→ No valid files found to process")


def default_output_for(input_path, command, opts):
    p = Path(input_path)
    if command == 'audio':
        ext = '.mp4' if opts.anim else '.png'
        return str(p.parent / f"{p.stem}_neon{ext}")
    if command == 'mesh':
        if opts.turntable > 0:
            return str(p.parent / f"{p.stem}_neon_turntable.mp4")
        return str(p.parent / f"{p.stem}_neon.png")
    return str(p.parent / f"{p.stem}_neon{p.suffix}")


def output_for_arg(input_path, output_arg, command, opts):
    output_path = Path(output_arg)
    is_folder = (str(output_arg).endswith('/') or
                 str(output_arg).endswith('\\') or
                 output_path.is_dir() or
                 (output_path.suffix == '' and not output_path.exists()))
    if is_folder:
        return str(output_path / Path(default_output_for(input_path, command, opts)).name)
    if command == 'audio' and opts.anim and output_path.suffix.lower() not in VIDEO_EXTENSIONS:
        return str(output_path) + '.mp4'
    return str(output_arg)


def process_video_neon(video_path, output_path, opts):
    ensure_ffmpeg()
    original_fps, frame_count, width, height = get_video_info(video_path)
    frames_dir = os.path.join(TEMP_DIR, "frames")
    neon_dir = os.path.join(TEMP_DIR, "neon")
    audio_path = os.path.join(TEMP_DIR, "audio.aac")
    if os.path.exists(TEMP_DIR):
        shutil.rmtree(TEMP_DIR)
    os.makedirs(TEMP_DIR, exist_ok=True)
    os.makedirs(neon_dir, exist_ok=True)
    try:
        progress.set_step("Extracting frames")
        progress.print_status()
        extract_frames(video_path, frames_dir, desc="Extracting frames")
        has_audio = extract_audio(video_path, audio_path)
        env = None
        if opts.pulse != 'off' and has_audio:
            frames = sorted([f for f in os.listdir(frames_dir) if f.endswith('.png')])
            env = audio_envelope_for_frames(audio_path, len(frames), original_fps)
            if env is None and opts.pulse == 'on':
                print("Warning: no usable audio track found, pulse disabled")
        elif opts.pulse == 'on' and not has_audio:
            print("Warning: no audio track found, pulse disabled")
        frames = sorted([f for f in os.listdir(frames_dir) if f.endswith('.png')])
        progress.set_step("Neonizing frames")
        progress.print_status()
        with tqdm(total=len(frames), desc="Neonizing frames", unit="frame",
                  bar_format="{desc} | Frame {n_fmt}/{total_fmt} {bar:25} {percentage:5.1f}% | {elapsed}<{remaining}, {rate_fmt}") as pbar:
            for i, frame_name in enumerate(frames):
                img = cv2.imread(os.path.join(frames_dir, frame_name))
                env_i = float(env[i]) if env is not None else 1.0
                out = process_image_neon(img, step_bar=_SilentStepBar(), palette=opts.palette,
                                         glow=opts.glow, threshold=opts.threshold, env=env_i)
                cv2.imwrite(os.path.join(neon_dir, frame_name), out)
                pbar.update(1)
        progress.set_step("Compiling video")
        progress.print_status()
        frames_to_video(neon_dir, output_path, original_fps,
                        audio_path if has_audio else None,
                        desc="Compiling video")
    finally:
        if os.path.exists(TEMP_DIR):
            shutil.rmtree(TEMP_DIR, ignore_errors=True)


def process_single_file(input_path, output_path, command, opts, step_bar=None):
    input_path = Path(input_path)
    if not input_path.exists():
        raise FileNotFoundError(f"Input not found: {input_path}")
    os.makedirs(os.path.dirname(str(output_path)) or '.', exist_ok=True)
    if command == 'neon':
        if is_video(str(input_path)):
            process_video_neon(str(input_path), str(output_path), opts)
        elif is_image(str(input_path)):
            img = cv2.imread(str(input_path))
            if img is None:
                raise ValueError(f"Failed to read image: {input_path}")
            result = process_image_neon(img, step_bar=step_bar, palette=opts.palette,
                                        glow=opts.glow, threshold=opts.threshold)
            success = cv2.imwrite(str(output_path), result)
            if not success:
                raise IOError(f"Failed to write image: {output_path}")
        else:
            raise ValueError(f"'neon' works on images and videos, got: {input_path.suffix}")
    elif command == 'audio':
        if not is_audio(str(input_path)):
            raise ValueError(f"'audio' works on audio files, got: {input_path.suffix}")
        if opts.anim:
            neonize_audio_anim(str(input_path), str(output_path), opts, step_bar=step_bar)
        else:
            neonize_audio_file(str(input_path), str(output_path), opts, step_bar=step_bar)
    elif command == 'mesh':
        if not is_mesh(str(input_path)):
            raise ValueError(f"'mesh' works on .obj and .stl files, got: {input_path.suffix}")
        neonize_mesh_file(str(input_path), str(output_path), opts, turntable=opts.turntable)
    else:
        raise ValueError(f"Unknown command: {command}")
    return output_path


def process_file_pairs(file_pairs, command, opts, file_type="file"):
    total_files = len(file_pairs)
    if total_files == 0:
        return []
    processed = 0
    errors = 0
    output_files = []
    progress.start_batch(total_files)
    print(f"\n{'='*60}")
    print(f"Processing {total_files} {file_type}(s) - Mode: {command}")
    print(f"Palette: {opts.palette} | Glow: {opts.glow:g} | Threshold: {opts.threshold:g}")
    print(f"{'='*60}\n")
    for i, (input_path, output_path) in enumerate(file_pairs, 1):
        try:
            progress.start_file(input_path)
            progress.set_step("Loading")
            progress.print_status(force=True)
            num_steps = 2 if (command == 'neon' and is_image(input_path)) else 1
            step_bar = StepProgressBar(num_steps, Path(input_path).name) if num_steps > 1 else None
            result = process_single_file(input_path, output_path, command, opts, step_bar=step_bar)
            if step_bar:
                step_bar.finish()
            elapsed = progress.finish_file()
            progress.set_step(f"Done ({elapsed:.1f}s)")
            progress.print_status(force=True)
            progress.print_newline()
            processed += 1
            if result:
                output_files.append(result)
        except Exception as e:
            progress.set_step(f"Error: {str(e)[:30]}")
            progress.print_status(force=True)
            progress.print_newline()
            errors += 1
    print(f"\n{'='*60}")
    print(f"Completed: {processed}/{total_files} {file_type}(s)")
    print(f"Total time: {progress.get_elapsed_str()}")
    if errors > 0:
        print(f"Errors: {errors}")
    print(f"{'='*60}")
    return output_files, processed, errors


def resolve_outputs(input_paths, command, opts, output_arg=None):
    pairs = []
    if output_arg:
        for input_path in input_paths:
            pairs.append((input_path, output_for_arg(input_path, output_arg, command, opts)))
    else:
        for input_path in input_paths:
            pairs.append((input_path, default_output_for(input_path, command, opts)))
    return pairs


def show_info():
    print("\n" + "="*60)
    print(f"NEONIFY - Procedural Neon Art Tool v{VERSION}")
    print("="*60)
    print(f"\nPython: {sys.version.split()[0]}")
    print(f"PyTorch: {torch.__version__}")
    print(f"OpenCV: {cv2.__version__}")
    print(f"NumPy: {np.__version__}")
    has_gpu, gpu_name, gpu_memory = check_gpu()
    print(f"CUDA available: {has_gpu}")
    if has_gpu:
        print(f"GPU: {gpu_name} ({gpu_memory})")
    print(f"ffmpeg: {'Available' if shutil.which('ffmpeg') else 'NOT FOUND'}")
    print(f"pyfiglet: {'Available' if HAS_PYFIGLET else 'Not installed (fallback banner)'}")
    print(f"\nPalettes: {', '.join(PALETTE_NAMES)}")
    print("\nInput types:")
    print(f"  Images: {', '.join(sorted(IMAGE_EXTENSIONS))}")
    print(f"  Videos: {', '.join(sorted(VIDEO_EXTENSIONS))}")
    print(f"  Audio:  {', '.join(sorted(AUDIO_EXTENSIONS))}")
    print(f"  Meshes: {', '.join(sorted(MESH_EXTENSIONS))}")
    print("\n" + "="*60)


def select_palette():
    print("\nSelect palette:")
    for i, name in enumerate(PALETTE_NAMES, 1):
        print(f"  {i}. {name.capitalize()}")
    while True:
        choice = input(f"\nSelect palette (1-{len(PALETTE_NAMES)}, default 1): ").strip()
        if choice == '' or choice == '1':
            return 'electric'
        if choice.isdigit() and 1 <= int(choice) <= len(PALETTE_NAMES):
            return PALETTE_NAMES[int(choice) - 1]
        print(f"Invalid choice '{choice}'. Please enter a number from 1 to {len(PALETTE_NAMES)}.")


def select_glow():
    print("\nGlow intensity options:")
    print("  1. Subtle (0.6)")
    print("  2. Normal (1.0, default)")
    print("  3. Strong (1.6)")
    print("  4. Extreme (2.4)")
    while True:
        choice = input("\nSelect glow (1-4, default 2): ").strip()
        if choice == '' or choice == '2':
            return 1.0
        if choice == '1':
            return 0.6
        if choice == '3':
            return 1.6
        if choice == '4':
            return 2.4
        print(f"Invalid choice '{choice}'. Please enter 1, 2, 3, or 4.")


def select_threshold():
    print("\nEdge threshold options:")
    print("  1. Sensitive - more edges, busier art (0.06)")
    print("  2. Balanced (0.12, default)")
    print("  3. Strict - only strong edges (0.22)")
    while True:
        choice = input("\nSelect threshold (1-3, default 2): ").strip()
        if choice == '' or choice == '2':
            return 0.12
        if choice == '1':
            return 0.06
        if choice == '3':
            return 0.22
        print(f"Invalid choice '{choice}'. Please enter 1, 2, or 3.")


def select_pulse():
    print("\nAudio pulse options (glow pulses to the beat):")
    print("  1. Auto - pulse if the video has sound (default)")
    print("  2. On - force pulse (warn if no sound)")
    print("  3. Off - constant glow")
    while True:
        choice = input("\nSelect pulse (1-3, default 1): ").strip()
        if choice == '' or choice == '1':
            return 'auto'
        if choice == '2':
            return 'on'
        if choice == '3':
            return 'off'
        print(f"Invalid choice '{choice}'. Please enter 1, 2, or 3.")


def select_audio_mode():
    print("\nAudio output type:")
    print("  1. Neon spectrogram art (PNG, default)")
    print("  2. Animated spectrum video (MP4 with the original audio)")
    while True:
        choice = input("\nSelect output type (1-2, default 1): ").strip()
        if choice == '' or choice == '1':
            return 'art'
        if choice == '2':
            return 'anim'
        print(f"Invalid choice '{choice}'. Please enter 1 or 2.")


def select_mesh_mode():
    print("\nMesh output type:")
    print("  1. Single neon wireframe view (PNG, default)")
    print("  2. Turntable orbit video (MP4)")
    while True:
        choice = input("\nSelect output type (1-2, default 1): ").strip()
        if choice == '' or choice == '1':
            return 0
        if choice == '2':
            return select_turntable_frames()
        print(f"Invalid choice '{choice}'. Please enter 1 or 2.")


def select_turntable_frames():
    print("\nTurntable frames options:")
    print("  1. 60 frames (2s loop)")
    print("  2. 120 frames (4s loop, default)")
    print("  3. 240 frames (8s loop)")
    while True:
        choice = input("\nSelect frames (1-3, default 2): ").strip()
        if choice == '' or choice == '2':
            return 120
        if choice == '1':
            return 60
        if choice == '3':
            return 240
        print(f"Invalid choice '{choice}'. Please enter 1, 2, or 3.")


def select_float(prompt, default, lo, hi):
    while True:
        raw = input(f"{prompt} [{lo:g}-{hi:g}] (Enter for {default:g}): ").strip()
        if raw == '':
            return default
        try:
            value = float(raw)
            if lo <= value <= hi:
                return value
            print(f"Value must be between {lo:g} and {hi:g}.")
        except ValueError:
            print(f"Invalid number '{raw}'.")


def interactive_mode():
    print_banner()
    if os.path.exists(TEMP_DIR):
        try:
            shutil.rmtree(TEMP_DIR)
        except Exception:
            pass
    while True:
        while True:
            print("\n" + "-"*60)
            print("INPUT SELECTION")
            print("-"*60)
            print("\nEnter input path(s) - separate multiple paths with spaces")
            print("Images, videos, audio and meshes can be mixed freely")
            print("Use quotes for paths with spaces, or 'q' to quit:")
            input_arg = input("> ").strip()
            if input_arg.lower() in ['q', 'quit', 'exit']:
                print("\nExiting Neonify. Stay glowing!")
                return
            if not input_arg:
                print("Error: No input provided. Please enter at least one path or 'q' to quit.")
                continue
            parsed_paths = parse_multiple_paths(input_arg)
            if not parsed_paths:
                print("Error: Could not parse any paths from input. Please try again.")
                continue
            categorized = categorize_multiple_paths(parsed_paths)
            display_path_summary(categorized)
            input_paths = categorized['all_valid_files']
            if not input_paths:
                print("\n" + "-"*40)
                has_errors = categorized['not_exist'] or categorized['not_supported'] or categorized['invalid']
                if has_errors:
                    print("Options:")
                    print("  1. Enter different paths")
                    print("  2. Exit")
                    retry_choice = input("\nSelect option (1 or 2): ").strip()
                    if retry_choice == '1':
                        continue
                    else:
                        print("\nExiting Neonify. Stay glowing!")
                        return
                else:
                    print("Please enter valid paths.")
                    continue
            else:
                break
        files_by_type = {
            'images': [f for f in input_paths if is_image(f)],
            'videos': [f for f in input_paths if is_video(f)],
            'audio': [f for f in input_paths if is_audio(f)],
            'meshes': [f for f in input_paths if is_mesh(f)],
        }
        print("\n" + "-"*60)
        print("FILES TO PROCESS")
        print("-"*60)
        print(f"  Images: {len(files_by_type['images'])}")
        print(f"  Videos: {len(files_by_type['videos'])}")
        print(f"  Audio:  {len(files_by_type['audio'])}")
        print(f"  Meshes: {len(files_by_type['meshes'])}")
        print("\n" + "-"*60)
        print("SHARED SETTINGS")
        print("-"*60)
        opts = argparse.Namespace()
        opts.palette = select_palette()
        opts.glow = select_glow()
        opts.threshold = select_threshold()
        if files_by_type['videos']:
            print("\n" + "-"*60)
            print("VIDEO SETTINGS")
            print("-"*60)
            opts.pulse = select_pulse()
        else:
            opts.pulse = 'auto'
        if files_by_type['audio']:
            print("\n" + "-"*60)
            print("AUDIO SETTINGS")
            print("-"*60)
            opts.anim = select_audio_mode() == 'anim'
        else:
            opts.anim = False
        if files_by_type['meshes']:
            print("\n" + "-"*60)
            print("MESH SETTINGS")
            print("-"*60)
            opts.turntable = select_mesh_mode()
            if opts.turntable == 0:
                opts.azimuth = select_float("Azimuth angle", 30.0, -180.0, 180.0)
                opts.elevation = select_float("Elevation angle", 20.0, -89.0, 89.0)
            else:
                opts.azimuth = 30.0
                opts.elevation = 20.0
        else:
            opts.turntable = 0
            opts.azimuth = 30.0
            opts.elevation = 20.0
        print("\n" + "-"*60)
        print("OUTPUT SETTINGS")
        print("-"*60)
        print("\nPress Enter for auto-default, or enter custom path.")
        print("For folders: outputs to that folder with _neon names.")
        print("For files: outputs next to the original with a _neon suffix.\n")
        input_outputs = {}
        for idx, input_path in enumerate(input_paths, 1):
            p = Path(input_path)
            if p.is_dir():
                continue
            if is_image(input_path) or is_video(input_path):
                command = 'neon'
            elif is_audio(input_path):
                command = 'audio'
            else:
                command = 'mesh'
            default_output = default_output_for(input_path, command, opts)
            display_name = p.name
            print(f"[{idx}/{len(input_paths)}] \"{display_name}\"")
            print(f"  Auto: {default_output}")
            user_output = input("> ").strip()
            if user_output:
                if user_output.startswith('"') and user_output.endswith('"'):
                    user_output = user_output[1:-1]
                input_outputs[input_path] = output_for_arg(input_path, user_output, command, opts)
            else:
                input_outputs[input_path] = default_output
        print("\n" + "-"*60)
        print("DEVICE SELECTION")
        print("-"*60)
        select_device()
        pairs_by_command = {'neon': [], 'audio': [], 'mesh': []}
        for input_path in input_paths:
            p = Path(input_path)
            if p.is_dir():
                folder_files = get_files(input_path)
                for f in folder_files:
                    if is_image(f) or is_video(f):
                        pairs_by_command['neon'].append((f, input_outputs.get(f, default_output_for(f, 'neon', opts))))
                    elif is_audio(f):
                        pairs_by_command['audio'].append((f, default_output_for(f, 'audio', opts)))
                    elif is_mesh(f):
                        pairs_by_command['mesh'].append((f, default_output_for(f, 'mesh', opts)))
                continue
            if is_image(input_path) or is_video(input_path):
                pairs_by_command['neon'].append((input_path, input_outputs[input_path]))
            elif is_audio(input_path):
                pairs_by_command['audio'].append((input_path, input_outputs[input_path]))
            elif is_mesh(input_path):
                pairs_by_command['mesh'].append((input_path, input_outputs[input_path]))
        all_outputs = []
        if pairs_by_command['neon']:
            result_files, _, _ = process_file_pairs(pairs_by_command['neon'], 'neon', opts, file_type="image/video")
            all_outputs.extend(result_files)
        if pairs_by_command['audio']:
            result_files, _, _ = process_file_pairs(pairs_by_command['audio'], 'audio', opts, file_type="audio")
            all_outputs.extend(result_files)
        if pairs_by_command['mesh']:
            result_files, _, _ = process_file_pairs(pairs_by_command['mesh'], 'mesh', opts, file_type="mesh")
            all_outputs.extend(result_files)
        print("\n" + "="*60)
        print("ALL PROCESSING COMPLETE!")
        print("="*60)
        if all_outputs:
            print("\nGenerated files:")
            for out in all_outputs:
                print(f"  {out}")
        print("\nWhat would you like to do next?")
        print("  1. Process again (start fresh)")
        print("  2. Exit")
        post_choice = input("\nSelect option (1 or 2): ").strip()
        if post_choice == '1':
            print("\nStarting fresh session...")
            if os.path.exists(TEMP_DIR):
                try:
                    shutil.rmtree(TEMP_DIR)
                except Exception:
                    pass
            continue
        else:
            print("\nExiting Neonify. Stay glowing!")
            return


def main():
    global JSON_PROGRESS

    parser = argparse.ArgumentParser(
        description=f"NEONIFY - Procedural Neon Art Tool v{VERSION}",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  neonify                                  # Launch GUI (default)
  neonify cli                              # Interactive CLI mode
  neonify neon photo.jpg                   # Neonize an image
  neonify neon clip.mp4 --pulse on         # Neonize video, glow pulses to the beat
  neonify audio song.mp3                   # Neon spectrogram art
  neonify audio song.mp3 --anim            # Animated spectrum video with audio
  neonify mesh model.obj --turntable 120   # Neon wireframe turntable
  neonify neon art.png --palette synthwave --glow 1.6 -o out.png
        """
    )
    parser.add_argument('command', nargs='?', default='gui',
                        help="Command: gui (default), cli, neon, audio, mesh, info")
    parser.add_argument('input', nargs='?', help="Input file or folder")
    parser.add_argument('-o', '--output', help="Output file or folder")
    parser.add_argument('--palette', choices=PALETTE_NAMES, default='electric',
                        help="Neon palette (default: electric)")
    parser.add_argument('--glow', type=float, default=1.0,
                        help="Glow intensity multiplier (0.2-3.0, default 1.0)")
    parser.add_argument('--threshold', type=float, default=0.12,
                        help="Edge detection threshold (0.02-0.5, default 0.12)")
    parser.add_argument('--pulse', choices=['auto', 'on', 'off'], default='auto',
                        help="Audio-reactive glow pulse for videos (default: auto)")
    parser.add_argument('--anim', action='store_true',
                        help="Audio: render animated spectrum video instead of spectrogram art")
    parser.add_argument('--turntable', type=int, default=0,
                        help="Mesh: render an orbit video with N frames instead of a single view")
    parser.add_argument('--azimuth', type=float, default=30.0,
                        help="Mesh: camera azimuth angle in degrees (default 30)")
    parser.add_argument('--elevation', type=float, default=20.0,
                        help="Mesh: camera elevation angle in degrees (default 20)")
    parser.add_argument('--device', choices=['cpu', 'gpu', 'auto'], default='auto',
                        help="Device to use (cpu, gpu, or auto)")
    parser.add_argument('--cpu', action='store_true', help="Force CPU (legacy, same as --device cpu)")
    parser.add_argument('--json-progress', action='store_true', help="Output progress as JSON (for GUI)")
    args = parser.parse_args()

    JSON_PROGRESS = args.json_progress

    if args.command == 'gui':
        try:
            gui_path = os.path.join(SCRIPT_DIR, "gui.py")
            if os.path.exists(gui_path):
                from gui import main as gui_main
                gui_main()
            else:
                print("Error: gui.py not found. Please ensure gui.py is in the same directory.")
                print("Falling back to CLI mode...")
                interactive_mode()
        except ImportError as e:
            print(f"Error: Could not import GUI: {e}")
            print("Make sure PyQt5 is installed: pip install PyQt5")
            print("Falling back to CLI mode...")
            interactive_mode()
        return

    if args.command == 'cli':
        interactive_mode()
        return

    if args.command == 'info':
        show_info()
        return

    if args.command not in ('neon', 'audio', 'mesh'):
        print(f"Error: Unknown command '{args.command}'")
        print("Commands: gui (default), cli, neon, audio, mesh, info")
        return

    if not args.input:
        print("Error: Input path required")
        print("Usage: python neonify.py <command> <input> [-o output]")
        print("\nCommands: neon (images/videos), audio, mesh")
        return

    if not (0.2 <= args.glow <= 3.0):
        print("Error: --glow must be between 0.2 and 3.0")
        return
    if not (0.02 <= args.threshold <= 0.5):
        print("Error: --threshold must be between 0.02 and 0.5")
        return
    if args.turntable < 0:
        print("Error: --turntable must be 0 (single view) or a positive frame count")
        return

    if os.path.exists(TEMP_DIR):
        try:
            shutil.rmtree(TEMP_DIR)
        except Exception:
            pass

    force_cpu = args.cpu or args.device == 'cpu'
    device_type = None if args.device == 'auto' else args.device
    get_device(force_cpu=force_cpu, device_type=device_type)

    input_paths = get_files(args.input)
    if not input_paths:
        if os.path.exists(args.input):
            input_paths = [args.input]
        else:
            print(f"Error: Input not found: {args.input}")
            return

    pairs = resolve_outputs(input_paths, args.command, args, output_arg=args.output)
    _, processed, _ = process_file_pairs(
        pairs, args.command, args,
        file_type={'neon': 'image/video', 'audio': 'audio', 'mesh': 'mesh'}[args.command])

    if processed == 0:
        sys.exit(1)

    if JSON_PROGRESS:
        json_output = json.dumps({
            'percent': 100,
            'step': 'Complete',
            'output': pairs[0][1] if pairs else ''
        })
        print(json_output)
        sys.stdout.flush()


if __name__ == '__main__':
    main()
