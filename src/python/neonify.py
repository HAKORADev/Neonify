#!/usr/bin/env python3
# NEONIFY — dark&white neon media neonifier (python reference engine)
# torch-free. numpy + opencv + ffmpeg only. CPU-native by design.
import os
import sys
import json
import math
import time
import shutil
import wave
import subprocess
import argparse
import datetime
import tempfile
import numpy as np

try:
    import cv2
except ImportError:
    cv2 = None

AUDIO_SR = 22050
APP_NAME = "NEONIFY"
APP_TAG = "dark&white neon media neonifier"
APP_VER = "v0.5.0"

PALETTE_NAMES = ['electric', 'crimson', 'ice', 'toxic', 'violet', 'golden', 'ghost']

# ------------------------------------------------------------------ banner
# built-in 5x5 font (no pyfiglet needed) — the banner the owner liked
_BANNER_FONT = {
    'N': ["10001", "11001", "10101", "10011", "10001"],
    'E': ["11111", "10000", "11110", "10000", "11111"],
    'O': ["01110", "10001", "10001", "10001", "01110"],
    'I': ["11111", "00100", "00100", "00100", "11111"],
    'F': ["11111", "10000", "11110", "10000", "10000"],
    'Y': ["10001", "10001", "01010", "00100", "00100"],
    ' ': ["00000", "00000", "00000", "00000", "00000"],
    ':': ["00000", "00100", "00000", "00100", "00000"],
    '-': ["00000", "00000", "11111", "00000", "00000"],
    '&': ["01100", "01100", "01010", "01101", "01110"],
    'W': ["10001", "10001", "10101", "11011", "10001"],
    'H': ["10001", "10001", "11111", "10001", "10001"],
    'T': ["11111", "00100", "00100", "00100", "00100"],
    'E'.lower(): ["01110", "10000", "11110", "10000", "01110"],
    'D': ["11110", "10001", "10001", "10001", "11110"],
    'M': ["10001", "11011", "10101", "10001", "10001"],
    'A': ["01110", "10001", "11111", "10001", "10001"],
    'V': ["10001", "10001", "10001", "01010", "00100"],
    'R': ["11110", "10001", "11110", "10010", "10001"],
    '.': ["00000", "00000", "00000", "00000", "00100"],
    '0': ["01110", "10011", "10101", "11001", "01110"],
    '5': ["11111", "10000", "11110", "00001", "11110"],
    '.': ["00000", "00000", "00000", "00000", "00100"],
}

NEON_BLUE = "\033[38;5;39m"
NEON_PINK = "\033[38;5;213m"
NEON_DIM = "\033[38;5;245m"
NEON_BOLD = "\033[1m"
NEON_RESET = "\033[0m"


def _banner_text():
    word = "NEONIFY"
    rows = ["", "", "", "", ""]
    for ch in word:
        glyph = _BANNER_FONT.get(ch, _BANNER_FONT[' '])
        for r in range(5):
            rows[r] += glyph[r].replace('1', '\u2588').replace('0', ' ') + " "
    return rows


def print_banner():
    rows = _banner_text()
    if not sys.stdout.isatty():
        print(APP_NAME)
        return
    print(f"{NEON_BLUE}{NEON_BOLD}{rows[0]}{NEON_RESET}")
    print(f"{NEON_BLUE}{rows[1]}{NEON_RESET}{NEON_DIM}  {APP_TAG}{NEON_RESET}")
    print(f"{NEON_PINK}{rows[2]}{NEON_RESET}")
    print(f"{NEON_PINK}{rows[3]}{NEON_RESET}{NEON_DIM}  {APP_VER}  \u00b7  cpu-native \u00b7 no gpu needed{NEON_RESET}")
    print(f"{NEON_BLUE}{rows[4]}{NEON_RESET}")
    print()


# ------------------------------------------------------------------ progress
# THE LAW (owner's fix): n/n counts COMPLETED steps and only increments when a
# step truly finishes. the % moves INSIDE the current step as real sub-work
# happens. no more "1/1 while still working on it".


class StageTracker:
    """stage-based progress: [3/7] 47%  current-stage-name
    json mode emits {"percent": n, "step": "..."} lines for the GUI pipe"""

    def __init__(self, enabled=True, json_mode=False):
        self.enabled = enabled
        self.json_mode = json_mode
        self.stages = []
        self.current = -1
        self.pct = 0.0
        self.last_line = ""
        self.t0 = time.time()
        self.stage_t0 = time.time()

    def set_stages(self, names):
        self.stages = list(names)
        self.current = -1
        self.pct = 0.0

    def begin_stage(self, idx, name=None):
        if idx >= len(self.stages):
            return
        if self.current != idx:
            self.current = idx
            self.pct = 0.0
            self.stage_t0 = time.time()
            self._draw(name or self.stages[idx])

    def step(self, frac):
        """frac 0..1 inside current stage"""
        if not self.enabled or self.current < 0:
            return
        frac = max(0.0, min(1.0, float(frac)))
        new_pct = int(frac * 100)
        if new_pct != int(self.pct):
            self.pct = frac * 100
            self._draw()

    def complete_stage(self, idx=None):
        idx = self.current if idx is None else idx
        if 0 <= idx < len(self.stages):
            self.current = idx
            self.pct = 100.0
            self._draw()
        if self.enabled and not self.json_mode:
            sys.stdout.write("\n")
            sys.stdout.flush()
        self.current = min(self.current + 1, len(self.stages) - 1) if self.stages else -1
        if self.current >= 0 and idx is not None and idx + 1 < len(self.stages):
            self.pct = 0.0
            self.stage_t0 = time.time()
        self.last_line = ""

    def finish(self):
        if self.enabled and not self.json_mode:
            total = time.time() - self.t0
            sys.stdout.write(f"{NEON_DIM}  done in {self._fmt(total)}{NEON_RESET}\n")
            sys.stdout.flush()

    def _draw(self, name=None):
        if not self.enabled:
            return
        n = self.current + 1
        total = len(self.stages)
        label = name or (self.stages[self.current] if 0 <= self.current < len(self.stages) else "")
        if self.json_mode:
            overall = int((n - 1 + self.pct / 100.0) / max(1, total) * 100)
            sys.stdout.write(json.dumps({"percent": min(overall, 100), "step": label}) + "\n")
            sys.stdout.flush()
            return
        line = f"\r{NEON_BLUE}[{n}/{total}]{NEON_RESET} {int(self.pct):3d}%  {NEON_DIM}{label}{NEON_RESET}"
        if len(line) < len(self.last_line):
            line += " " * (len(self.last_line) - len(line))
        sys.stdout.write(line)
        sys.stdout.flush()
        self.last_line = line

    def _fmt(self, s):
        if s < 60:
            return f"{s:.0f}s"
        if s < 3600:
            return f"{s/60:.1f}m"
        return f"{s/3600:.1f}h"


class StepProgressBar:
    """legacy-compatible mini bar: fixed step list, prints one line per step"""

    def __init__(self, steps, file_name="", enabled=True):
        self.names = list(steps)
        self.done = set()
        self.file_name = file_name
        self.enabled = enabled

    def update(self, step_name):
        if step_name in self.names and self.enabled:
            self.done.add(step_name)
            n = len(self.done)
            total = len(self.names)
            sys.stdout.write(f"\r  {NEON_DIM}[{n}/{total}] {step_name}{NEON_RESET}\n")
            sys.stdout.flush()

    def finish(self):
        pass


class _SilentStepBar:
    def update(self, msg):
        pass

    def finish(self):
        pass


def last_stage(tracker):
    """returns a callable that pins the tracker at its final stage"""
    def _pin():
        if tracker.stages:
            tracker.begin_stage(len(tracker.stages) - 1)
    return _pin


# ------------------------------------------------------------------ paths
def timestamp_suffix():
    """VODER-style: _261002182032 (to seconds) — kills collisions, keeps copies"""
    return datetime.datetime.now().strftime("_%y%m%d%H%M%S")


def unique_output_path(path):
    """if path exists, append timestamp instead of overwriting"""
    if not os.path.exists(path):
        return path
    root, ext = os.path.splitext(path)
    return root + timestamp_suffix() + ext


def ensure_dir(d):
    if d and not os.path.isdir(d):
        os.makedirs(d, exist_ok=True)
    return d


def is_media_file(path, kinds):
    return os.path.isfile(path) and os.path.splitext(path)[1].lower() in kinds


IMG_EXTS = {'.png', '.jpg', '.jpeg', '.bmp', '.webp', '.tif', '.tiff'}
VID_EXTS = {'.mp4', '.avi', '.mkv', '.mov', '.webm', '.gif'}
AUD_EXTS = {'.wav', '.mp3', '.flac', '.ogg', '.m4a', '.aac', '.wma'}
MESH_EXTS = {'.obj', '.ply', '.stl'}


def collect_inputs(inputs, kinds):
    """expand folders → file lists (deterministic order)"""
    out = []
    for p in inputs:
        if os.path.isdir(p):
            for fn in sorted(os.listdir(p)):
                fp = os.path.join(p, fn)
                if is_media_file(fp, kinds):
                    out.append(fp)
        elif is_media_file(p, kinds):
            out.append(p)
    return out


# ================================================================== image
def _f32(x):
    """cv2 discipline: percentile/np math leaks float64; kill it at the door"""
    if x.dtype != np.float32:
        x = x.astype(np.float32)
    return np.ascontiguousarray(x)


def gaussian_blur(img, sigma):
    if sigma <= 0.05:
        return img
    k = int(math.ceil(sigma * 3.0)) * 2 + 1
    return _f32(cv2.GaussianBlur(img, (k, k), sigma, borderType=cv2.BORDER_REPLICATE))


def luminance(img):
    if img.ndim == 2:
        return img
    return _f32(img @ np.array([0.114, 0.587, 0.299], np.float32))


def smoothstep(lo, hi, x):
    t = np.clip((x - lo) / max(1e-6, (hi - lo)), 0.0, 1.0)
    return (t * t * (3.0 - 2.0 * t)).astype(np.float32)


def build_palette_lut(name, n=256):
    """returns (n,3) uint8 BGR ramp"""
    t = np.linspace(0, 1, n, dtype=np.float32)
    if name == 'crimson':
        r = 0.15 + 0.85 * t
        g = 0.02 * np.power(t, 2.2)
        b = 0.08 + 0.25 * np.power(t, 3.0) * (1 - t) * 2
    elif name == 'ice':
        r = 0.45 * np.power(t, 2.5)
        g = 0.55 + 0.45 * np.power(t, 1.6)
        b = 0.65 + 0.35 * t
    elif name == 'toxic':
        r = 0.45 * np.power(t, 3.2)
        g = 0.25 + 0.75 * t
        b = 0.06 * np.power(t, 1.4)
    elif name == 'violet':
        r = 0.35 + 0.65 * np.power(t, 1.3)
        g = 0.04 + 0.22 * np.power(t, 2.8)
        b = 0.55 + 0.45 * t
    elif name == 'golden':
        r = 0.55 + 0.45 * t
        g = 0.32 * np.power(t, 0.7) + 0.12 * np.power(t, 2.0)
        b = 0.02 * np.power(t, 2.4)
    elif name == 'ghost':
        r = 0.62 + 0.38 * np.power(t, 1.8)
        g = 0.68 + 0.32 * np.power(t, 1.4)
        b = 0.78 + 0.22 * t
    else:  # electric: deep blue → cyan → white-hot
        r = 0.85 * np.power(t, 3.4)
        g = 0.25 + 0.75 * np.power(t, 1.25)
        b = 0.10 + 0.90 * np.power(t, 0.55)
    lut = np.stack([b, g, r], axis=1)  # BGR
    return np.clip(lut * 255.0, 0, 255).astype(np.uint8)


_PAL_CACHE = {}


def palette_lut(name):
    lut = _PAL_CACHE.get(name)
    if lut is None:
        lut = build_palette_lut(name)
        _PAL_CACHE[name] = lut
    return lut


# --- edge field v2 -------------------------------------------------------
# owner's law: preserve TEXT accurately, survive noisy b&w low-quality video.
# recipe: XDoG core + support gate (kills isolated speckle, keeps real
# strokes) + auto re-denoise (noise estimate in flat regions drives a
# pre-blur) + halo unstacking (glow never stacks on hot cores → no blobs).

def _noise_estimate(lum):
    """robust noise sigma from median-absolute-Laplacian (flat areas dominate
    the median, so structure barely moves it; black frames give ~0 — correct)"""
    lap = cv2.Laplacian(lum, cv2.CV_32F, ksize=3)
    return float(np.median(np.abs(lap))) / 0.6745


def _xdog_response(lum, sigma, sharp, tau, phi_eps):
    """XDoG: difference of gaussians soft-thresholded"""
    g1 = gaussian_blur(lum, sigma)
    g2 = gaussian_blur(lum, sigma * 1.6)
    dog = (g1 - tau * g2) / 255.0
    u = sharp * (dog - 0.008)  # soft threshold
    return (1.0 / (1.0 + np.exp(np.clip(-phi_eps * u, -60.0, 60.0)))).astype(np.float32)


def _support_mask(lum, radius):
    """local structural support: strong where sustained edges exist.
    text strokes are sustained; sensor speckle is not."""
    g = gaussian_blur(lum, radius)
    grad = cv2.Sobel(g, cv2.CV_32F, 1, 0, ksize=3) ** 2 + \
        cv2.Sobel(g, cv2.CV_32F, 0, 1, ksize=3) ** 2
    grad = np.sqrt(grad)
    # support = broadened gradient energy (dilated a little so stroke
    # interiors survive), then normalized
    k = int(radius * 2) * 2 + 1
    support = cv2.dilate(grad, np.ones((k, k), np.float32))
    hi = np.percentile(support, 92)
    if hi < 1e-5:
        return np.zeros_like(support)
    return smoothstep(hi * 0.12, hi * 0.55, support)


def _sobel_mag(lum):
    gx = cv2.Sobel(lum, cv2.CV_32F, 1, 0, ksize=3)
    gy = cv2.Sobel(lum, cv2.CV_32F, 0, 1, ksize=3)
    return _f32(np.sqrt(gx * gx + gy * gy) / 255.0)


_GLOW_SIGMAS = (2.0, 6.0, 16.0, 38.0)
_GLOW_WEIGHTS = (0.9, 0.62, 0.45, 0.32)


def edge_field(img_bgr, glow=1.0, threshold=0.12, env=1.0):
    """v1 tube law + round-2 fixes. returns (edges, aux).
    edges = stroke ridges (0..1). glow built separately from these."""
    lum = luminance(img_bgr)

    # auto re-denoise: noisy b&w sources get exactly as much pre-blur as needed
    noise = _noise_estimate(lum)
    pre_sigma = float(np.clip((noise - 2.0) * 0.16, 0.0, 2.4))
    work = gaussian_blur(lum, pre_sigma) if pre_sigma > 0.05 else lum

    base = gaussian_blur(work, 1.0)
    mag = _sobel_mag(base)
    thr = threshold * (1.0 + pre_sigma * 0.38)  # noise raises the blade
    edge = smoothstep(thr, thr + 0.22, mag)

    # support gate: real strokes have sustained structure; speckle does not
    sup = _support_mask(work, 1.0 + pre_sigma)
    edge = edge * smoothstep(0.04, 0.30, sup)

    aux = {"noise": noise, "pre_sigma": pre_sigma, "edge": edge}
    return _f32(edge), aux


def _edge_density(edge):
    """fraction of active edge px — dense frames (wall-of-text) need calmer glow"""
    return float((edge > 0.35).mean())


def neon_glow_stack(img_bgr, edges, glow=1.0, env=1.0, threshold=0.12):
    """v1 glow stack + halo unstacking + density adaptation."""
    lum = luminance(img_bgr) if img_bgr is not None else None

    g = float(np.clip(glow, 0.1, 3.0))
    density = _edge_density(edges)
    calm = 1.0 / (1.0 + 3.5 * max(0.0, density - 0.18))  # wall-of-text: soften

    # halo unstacking: hot cores feed the glow less -> no white blobs over text
    src = edges * (1.0 - 0.55 * np.clip(edges * 1.25, 0, 1))

    glow_field = np.zeros_like(edges)
    for sigma, weight in zip(_GLOW_SIGMAS, _GLOW_WEIGHTS):
        glow_field = glow_field + gaussian_blur(src, sigma) * (weight * g * env * calm)

    excite = 0.72 + 0.42 * env
    energy = np.clip((edges * 1.15 + glow_field * 0.85) * excite, 0.0, 1.0)

    # core boost: the stroke itself burns hottest (the tube)
    core = np.power(edges, 1.2) * 0.85
    energy = energy + core * 0.55

    # spatial glow gets wired in by the video engine on `energy`
    return _f32(np.clip(energy, 0.0, 1.0))


def colorize(field, palette, lift=0.0):
    """map 0..1.35 intensity field through palette LUT (field may be HxW or HxWx3)"""
    lut = palette_lut(palette)
    x = field if field.ndim == 2 else field.mean(axis=-1)
    x = np.clip(x, 0.0, 1.0)
    idx = (x * 255.0).astype(np.uint8)
    col = lut[idx]  # HxWx3 uint8 BGR
    out = col.astype(np.float32)
    if lift > 0:
        out = out + lift * 255.0 * 0.045
    return np.clip(out, 0, 255).astype(np.uint8)


def process_image_neon(img_bgr, palette='electric', glow=1.0, threshold=0.12, env=1.0):
    edges, aux = edge_field(img_bgr, glow, threshold, env)
    field = neon_glow_stack(img_bgr, edges, glow=glow, env=env, threshold=threshold)
    return colorize(field, palette), aux


def process_image_neon_ex(img_bgr, palette='electric', glow=1.0, threshold=0.12, env=1.0):
    """same as process_image_neon but also returns the raw intensity field (for alpha)"""
    edges, aux = edge_field(img_bgr, glow, threshold, env)
    field = neon_glow_stack(img_bgr, edges, glow=glow, env=env, threshold=threshold)
    return colorize(field, palette), field, aux


def _apply_alpha(neon_bgr, field, alpha):
    """tubes on transparency: alpha = neon intensity * source alpha"""
    a = np.clip(field * 1.25, 0.0, 1.0) * alpha
    bgra = cv2.cvtColor(neon_bgr, cv2.COLOR_BGR2BGRA)
    bgra[:, :, 3] = (a * 255.0 + 0.5).astype(np.uint8)
    return bgra


def neonize_image_file(inp, out_path, palette='electric', glow=1.0,
                       threshold=0.12, env=1.0, tracker=None):
    img = cv2.imread(inp, cv2.IMREAD_UNCHANGED)
    if img is None:
        raise RuntimeError(f"cannot read image: {inp}")
    alpha = None
    if img.ndim == 3 and img.shape[2] == 4:
        alpha = img[:, :, 3].astype(np.float32) * (1.0 / 255.0)
        img = cv2.cvtColor(img, cv2.COLOR_BGRA2BGR)
    elif img.ndim == 2:
        img = cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)
    if tracker:
        tracker.step("neonizing")
    out, field, aux = process_image_neon_ex(img, palette, glow, threshold, env)
    if alpha is not None:
        out = _apply_alpha(out, field, alpha)
    if tracker:
        tracker.step("saving")
    out_path = unique_output_path(out_path)
    if not cv2.imwrite(out_path, out, [cv2.IMWRITE_PNG_COMPRESSION, 6]):
        raise RuntimeError(f"cannot write image: {out_path}")
    return out_path, aux


# ================================================================== audio
def _fftconvolve(x, h):
    """fft convolution (numpy only) with full-length output, same-length as x"""
    n = len(x) + len(h) - 1
    nfft = 1 << (n - 1).bit_length()
    X = np.fft.rfft(x, nfft)
    H = np.fft.rfft(h, nfft)
    y = np.fft.irfft(X * H, nfft)[:len(x)]
    return y.astype(np.float32)


def _lowpass_ir(cutoff, sr, tail=1.2):
    n = int(sr * tail) | 1
    t = (np.arange(n) - n // 2) / sr
    h = 2 * np.pi * cutoff * np.sinc(2 * np.pi * cutoff * t) * np.hanning(n)
    h = h / np.sum(np.abs(h))
    return h.astype(np.float32)


def lowpass(x, cutoff, sr):
    if cutoff >= sr * 0.48:
        return x
    return _fftconvolve(x, _lowpass_ir(cutoff, sr))


def highpass(x, cutoff, sr):
    return x - lowpass(x, cutoff, sr)


def soft_clip(x, drive=1.0):
    return np.tanh(x * drive).astype(np.float32)


def _peak(x):
    p = float(np.max(np.abs(x))) if x.size else 0.0
    return p if p > 1e-9 else 1.0


def _normalize_pair(l, r, target=0.89):
    p = max(_peak(l), _peak(r))
    k = target / p
    return (l * k).astype(np.float32), (r * k).astype(np.float32)


def _to_stereo(l, r):
    if l.ndim > 1:
        l = l.mean(axis=1)
    if r.ndim > 1:
        r = r.mean(axis=1)
    return l.astype(np.float32), r.astype(np.float32)


def decode_audio_stereo(path, sr=AUDIO_SR):
    """ffmpeg → float32 stereo (l, r). returns (None, None) reason string on fail"""
    try:
        cmd = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', path,
               '-f', 'f32le', '-acodec', 'pcm_f32le', '-ac', '2', '-ar', str(sr), '-']
        raw = subprocess.run(cmd, capture_output=True, timeout=600).stdout
        if not raw:
            return None, None
        x = np.frombuffer(raw, dtype=np.float32)
        if x.size < 256:
            return None, None
        if x.size % 2:
            x = x[:-1]
        x = x.reshape(-1, 2)
        return _to_stereo(x[:, 0].copy(), x[:, 1].copy())
    except Exception:
        return None, None


def write_wav_stereo(path, l, r, sr=AUDIO_SR):
    l, r = _normalize_pair(l, r)
    try:
        import soundfile as sf
        data = np.empty((len(l), 2), np.float32)
        data[:, 0] = l
        data[:, 1] = r
        sf.write(path, data, sr, subtype='PCM_16')
        return
    except Exception:
        pass
    data = np.empty((len(l), 2), np.int16)
    data[:, 0] = np.clip(l * 32767, -32768, 32767).astype(np.int16)
    data[:, 1] = np.clip(r * 32767, -32768, 32767).astype(np.int16)
    with wave.open(path, 'wb') as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(sr)
        w.writeframes(data.tobytes())


# ---- fx primitives ------------------------------------------------------
def fx_tube_drive(l, r, sr, amount):
    def shape(x):
        a = 1.0 + amount * 5.0
        warm = np.tanh(x * a) / np.tanh(a)
        body = lowpass(warm, 6500, sr)
        return soft_clip(body * (1.0 + 0.12 * amount), 1.2)
    return shape(l), shape(r)


def fx_pingpong_echo(l, r, sr, time_s, fb, damp_hz, mix):
    d = max(1, int(time_s * sr))
    n = len(l) + 2 * d
    acc_l = np.zeros(n, np.float32)
    acc_r = np.zeros(n, np.float32)
    acc_l[:len(l)] = l
    acc_r[:len(r)] = r
    gl, gr = fb, fb
    damp_l = lowpass(l, damp_hz, sr)
    damp_r = lowpass(r, damp_hz, sr)
    for i in range(1, 14):
        s = d * (i + 1)
        if s >= n:
            break
        seg = d
        gl *= fb
        gr *= fb
        seg_l = damp_l * gl
        seg_r = damp_r * gr
        if s + seg > n:
            seg = n - s
        acc_l[s:s + seg] += seg_r[:seg]  # ping-pong: crosses sides
        acc_r[s:s + seg] += seg_l[:seg]
    wet_l, wet_r = acc_l[:len(l)], acc_r[:len(r)]
    return ((1 - mix) * l + mix * wet_l, (1 - mix) * r + mix * wet_r)


def fx_void_reverb(l, r, sr, mix, tone_hz):
    """deep comb-bank reverb, dark tone"""
    n = len(l)
    rl = np.zeros(n, np.float32)
    rr = np.zeros(n, np.float32)
    for d_ms, g in ((61, 0.72), (89, 0.66), (127, 0.61), (173, 0.57), (211, 0.52), (293, 0.47)):
        d = max(1, int(d_ms * sr / 1000))
        w = np.zeros(n + d, np.float32)
        w[:n] = l if d_ms % 2 else r
        acc = np.zeros(n + d, np.float32)
        for rep in range(1, 6):
            s = d * rep
            if s >= len(w):
                break
            acc[s:] += w[:-s] * (g ** rep)
        acc = acc[:n]
        if d_ms % 2:
            rl += acc
        else:
            rr += acc
    rl = lowpass(rl / 6.0, tone_hz, sr)
    rr = lowpass(rr / 6.0, tone_hz, sr)
    return (1 - mix) * l + mix * rl, (1 - mix) * r + mix * rr


def fx_tremolo(l, r, sr, rate, depth):
    t = np.arange(len(l), dtype=np.float32) / sr
    m = 1.0 - depth * 0.5 * (1.0 - np.sin(2 * np.pi * rate * t))
    return l * m, r * m


def fx_vibrato(l, r, sr, rate, depth_ms):
    n = len(l)
    t = np.arange(n, dtype=np.float32) / sr
    delay = depth_ms * 0.001 * sr * (0.5 + 0.5 * np.sin(2 * np.pi * rate * t))
    idx = np.arange(n)[:, None] - delay[::1][None, :].T[0][:, None].T if False else None
    # linear-interp variable delay, vectorized per block
    def bend(x):
        out = np.empty(n, np.float32)
        base = np.arange(n)
        pos = base - delay
        i0 = np.floor(pos).astype(np.int64)
        frac = (pos - i0).astype(np.float32)
        i0c = np.clip(i0, 0, n - 1)
        i1c = np.clip(i0 + 1, 0, n - 1)
        out = x[i0c] * (1 - frac) + x[i1c] * frac
        return out.astype(np.float32)
    return bend(l), bend(r)


def fx_ring_mod(l, r, sr, hz, mix, depth=0.85):
    t = np.arange(len(l), dtype=np.float32) / sr
    car = np.sin(2 * np.pi * hz * t) * depth + (1 - depth)
    return (l * (1 - mix) + l * car * mix, r * (1 - mix) + r * car * mix)


def fx_crush(l, r, sr, bits):
    q = 2.0 ** (bits - 1)
    return np.round(l * q) / q, np.round(r * q) / q


def fx_slash_sweep(l, r, sr, gain):
    """the signature diagonal energy sweep (kept from v0.5)"""
    n = len(l)
    t = np.linspace(0, 1, n, dtype=np.float32)
    sweep = np.sin(np.pi * t) ** 2 * gain
    off = np.sin(2 * np.pi * 0.5 * t) * 0.4
    return l * (1 + sweep * (0.6 + off)), r * (1 + sweep * (0.6 - off))


# ---- spectral tools (ice shimmer, void pitch) ---------------------------
def stft_mag_phase(x, n_fft=2048, hop=512):
    w = np.hanning(n_fft).astype(np.float32)
    frames = 1 + max(0, (len(x) - n_fft + hop - 1)) // hop  # ceil: cover the tail
    pad = frames * hop + n_fft - len(x)
    if pad > 0:
        x = np.concatenate([x, np.zeros(pad, np.float32)])
    S = np.zeros((n_fft // 2 + 1, frames), np.complex64)
    for i in range(frames):
        seg = x[i * hop:i * hop + n_fft] * w
        S[:, i] = np.fft.rfft(seg)
    return S


def istft(S, hop=512, length=None):
    n_fft = 2 * (S.shape[0] - 1)
    w = np.hanning(n_fft).astype(np.float32)
    out = np.zeros(n_fft + (S.shape[1] - 1) * hop, np.float32)
    win_sum = np.zeros_like(out)
    for i in range(S.shape[1]):
        seg = np.fft.irfft(S[:, i], n_fft) * w
        out[i * hop:i * hop + n_fft] += seg
        win_sum[i * hop:i * hop + n_fft] += w
    out = out / np.maximum(win_sum, 1e-6)
    if length:
        if len(out) < length:
            out = np.concatenate([out, np.zeros(length - len(out), np.float32)])
        out = out[:length]
    return out


def _spectral_shift(x, sr, semitones):
    """pitch shift via resample of spectrum bins (naive but clean for FX)"""
    if abs(semitones) < 0.01:
        return x
    ratio = 2.0 ** (semitones / 12.0)
    n_fft, hop = 2048, 512
    S = stft_mag_phase(x, n_fft, hop)
    bins = S.shape[0]
    new_bins = bins
    idx = np.linspace(0, bins - 1, new_bins)
    i0 = np.floor(idx).astype(np.int64)
    i1 = np.clip(i0 + 1, 0, bins - 1)
    frac = (idx - i0).astype(np.float32)
    out_S = np.zeros_like(S)
    shift = int(round((1 - ratio) * bins / 2))
    src = S * 0
    moved = np.zeros_like(S)
    lo = max(0, shift)
    hi = bins + min(0, shift)
    if shift != 0:
        moved[lo:hi] = S[lo - shift:hi - shift]
    else:
        moved[:] = S
    mag = np.abs(moved)
    out_S = moved * 0 + moved  # keep phase; magnitude move is enough for FX
    return istft(out_S, hop, len(x))


def fx_ice_shimmer(l, r, sr, mix):
    """ice-steam feel: up-shifted shimmer layer + airy high shelf"""
    def one(x):
        sh = _spectral_shift(x, sr, 12.0)  # octave up
        sh = lowpass(sh, 9000, sr)
        air = highpass(x, 6000, sr) * 0.25
        return x * (1 - mix) + (sh * 0.45 + air * 0.5) * mix
    return one(l), one(r)


def fx_ghost_fog(l, r, sr, mix):
    """foggy: slow-breathing dark reverb + whisper detune doubles"""
    t = np.arange(len(l), dtype=np.float32) / sr
    breathe = 0.5 + 0.5 * np.sin(2 * np.pi * 0.13 * t)
    rl, rr = fx_void_reverb(l, r, sr, 1.0, 1800)
    fog_l = lowpass(rl, 1200, sr) * (1 - breathe * 0.45) + lowpass(rl, 2800, sr) * (breathe * 0.45)
    fog_r = lowpass(rr, 1200, sr) * (1 - (1 - breathe) * 0.45) + lowpass(rr, 2800, sr) * ((1 - breathe) * 0.45)
    dl, dr = fx_vibrato(l, r, sr, 0.4, 6.0)
    return ((1 - mix) * l + mix * (fog_l * 0.8 + dl * 0.2),
            (1 - mix) * r + mix * (fog_r * 0.8 + dr * 0.2))


def fx_fire_burn(l, r, sr, glow):
    """fire = burn: crackle + rumble + flicker tremolo + heat drive"""
    rng = np.random.default_rng(261002)
    n = len(l)

    def crackle():
        imp = (rng.random(n) < (0.00008 * (1 + glow)))
        burst = imp.astype(np.float32) * (rng.random(n) * 0.8 + 0.2)
        pop = lowpass(burst, 3800, sr) * 6.0
        return pop

    def rumble():
        wn = rng.standard_normal(n).astype(np.float32)
        return lowpass(wn, 90, sr) * 2.2

    cl, cr = crackle(), crackle() * 0.6
    rum = rumble()
    out_l, out_r = fx_tube_drive(l, r, sr, 0.35 + 0.2 * min(glow, 2))
    out_l, out_r = fx_tremolo(out_l, out_r, sr, 5.5, 0.18)  # flicker
    out_l = out_l + cl * 0.5 + rum * 0.7
    out_r = out_r + cr * 0.5 + rum * 0.7
    return out_l, out_r


def fx_robotic(l, r, sr, glow):
    """robotic: ring mod + formant combs + flat crush"""
    l2, r2 = fx_ring_mod(l, r, sr, 88.0, 0.65)
    d1 = max(1, int(0.021 * sr))
    d2 = max(1, int(0.037 * sr))
    cl = l2 - np.concatenate([np.zeros(d1, np.float32), l2[:-d1]]) * 0.45
    cr = r2 - np.concatenate([np.zeros(d2, np.float32), r2[:-d2]]) * 0.45
    cl, cr = fx_crush(cl, cr, sr, 10)
    return cl, cr


def fx_void_pitch(l, r, sr, mix):
    """void: down an octave, dark, wide"""
    l2 = _spectral_shift(lowpass(l, 4000, sr), sr, -12.0)
    r2 = _spectral_shift(lowpass(r, 4000, sr), sr, -12.0)
    wide_l = 0.5 * (l + l2)
    wide_r = 0.5 * (r - r2)  # inverted for width
    return (1 - mix) * l + mix * wide_l, (1 - mix) * r + mix * wide_r


# ---- the profile system -------------------------------------------------
# shared settings became PROFILES. each profile is an ordered fx chain with
# its own dynamic response to glow (owner: "not the same first settings for
# all of stuff"). advanced settings override any step's parameters.

AUDIO_PROFILES = ['fire', 'ice', 'robotic', 'ghost', 'void', 'echo', 'slash']

PROFILE_DESCRIPTIONS = {
    'fire': 'burn — crackle, rumble, flicker, heat drive',
    'ice': 'ice-steam — octave shimmer, airy shelf, glassy breath',
    'robotic': 'metal ring-mod, formant combs, crushed edges',
    'ghost': 'fog — breathing dark reverb, whisper detune',
    'void': 'the abyss — octave-down, huge dark space',
    'echo': 'proper clean ping-pong echo, tone-shaped',
    'slash': 'the signature diagonal energy sweep',
}


def apply_audio_profile(l, r, sr, profile='slash', glow=1.0, advanced=None, on_step=None):
    """run profile chain step by step. advanced: dict overriding step params.
    on_step(name, frac) reports real chain progress. returns (l, r, steps_done)"""
    g = float(np.clip(glow, 0.1, 3.0))
    adv = dict(advanced or {})

    def over(key, default):
        return adv.get(key, default)

    chain = []

    if profile == 'fire':
        chain.append(('heat drive', lambda: fx_tube_drive(l, r, sr, over('drive', 0.3 + 0.22 * min(g, 2)))))
        chain.append(('flicker', lambda: fx_tremolo(l, r, sr, over('flicker_rate', 5.5), over('flicker_depth', min(0.14 + 0.05 * g, 0.5)))))
        def _crackle_pair():
            rng1 = np.random.default_rng(261002)
            rng2 = np.random.default_rng(726100)
            n = len(l)
            c1 = lowpass((rng1.random(n) < (0.00008 * (1 + g))).astype(np.float32) * 6.0, 3800, sr) * over('crackle', 0.5)
            c2 = lowpass((rng2.random(n) < (0.00006 * (1 + g))).astype(np.float32) * 6.0, 3800, sr) * over('crackle', 0.5)
            return l + c1, r + c2
        chain.append(('crackle', _crackle_pair))
        def _rumble_pair():
            rng1 = np.random.default_rng(112233)
            rng2 = np.random.default_rng(332211)
            n = len(l)
            r1 = lowpass(rng1.standard_normal(n).astype(np.float32), over('rumble_hz', 90), sr) * over('rumble', 0.6)
            r2 = lowpass(rng2.standard_normal(n).astype(np.float32), over('rumble_hz', 90), sr) * over('rumble', 0.6)
            return l + r1, r + r2
        chain.append(('rumble', _rumble_pair))

    elif profile == 'ice':
        chain.append(('shimmer', lambda: fx_ice_shimmer(l, r, sr, over('shimmer_mix', 0.28 + 0.1 * min(g, 2)))))
        chain.append(('glass breath', lambda: fx_vibrato(l, r, sr, over('breath_rate', 0.5), over('breath_depth', 3.5))))
        chain.append(('frost echo', lambda: fx_pingpong_echo(l, r, sr, over('time', 0.19), over('fb', 0.3), over('damp', 7000), over('mix', 0.2 + 0.06 * g))))

    elif profile == 'robotic':
        chain.append(('ring mod', lambda: fx_ring_mod(l, r, sr, over('ring_hz', 88.0), over('ring_mix', 0.6))))
        def _combs():
            d1 = max(1, int(0.021 * sr))
            d2 = max(1, int(0.037 * sr))
            cl = l - np.concatenate([np.zeros(d1, np.float32), l[:-d1]]) * over('comb', 0.45)
            cr = r - np.concatenate([np.zeros(d2, np.float32), r[:-d2]]) * over('comb', 0.45)
            return cl, cr
        chain.append(('formant combs', _combs))
        chain.append(('crush', lambda: fx_crush(l, r, sr, int(over('bits', 10)))))

    elif profile == 'ghost':
        chain.append(('fog reverb', lambda: fx_ghost_fog(l, r, sr, over('fog_mix', 0.42 + 0.08 * g))))
        chain.append(('whisper detune', lambda: fx_vibrato(l, r, sr, over('whisper_rate', 0.37), over('whisper_depth', 5.0))))
        chain.append(('far echo', lambda: fx_pingpong_echo(l, r, sr, over('time', 0.42), over('fb', 0.42), over('damp', 2600), over('mix', 0.3))))

    elif profile == 'void':
        chain.append(('descent', lambda: fx_void_pitch(l, r, sr, over('pitch_mix', 0.45))))
        chain.append(('abyss reverb', lambda: fx_void_reverb(l, r, sr, over('mix', 0.4 + 0.08 * g), over('tone', 1500))))
        chain.append(('cave echo', lambda: fx_pingpong_echo(l, r, sr, over('time', 0.55), over('fb', 0.5), over('damp', 1800), over('mix', 0.3))))

    elif profile == 'echo':
        chain.append(('ping-pong', lambda: fx_pingpong_echo(l, r, sr, over('time', 0.31), over('fb', 0.45), over('damp', 4200), over('mix', 0.35))))
        chain.append(('tone', lambda: (lowpass(l, over('lp', 9000), sr), lowpass(r, over('lp', 9000), sr))))

    else:  # slash (default signature)
        chain.append(('slash sweep', lambda: fx_slash_sweep(l, r, sr, over('gain', 0.4 + 0.15 * g))))

    total = len(chain)
    steps_done = []
    for i, (name, fn) in enumerate(chain):
        if on_step:
            on_step(name, i / total)
        l, r = fn()
        steps_done.append(name)
        if on_step:
            on_step(name, (i + 1) / total)

    l, r = _normalize_pair(l, r)
    return l.astype(np.float32), r.astype(np.float32), steps_done


def neonize_audio_file(inp, out_path, profile='slash', glow=1.0, advanced=None, tracker=None):
    l, r = decode_audio_stereo(inp)
    if l is None:
        raise RuntimeError(f"cannot decode audio: {inp}")
    if tracker:
        tracker.set_stages(['decode', 'profile chain', 'mastering', 'write'])
        tracker.begin_stage(0, 'decode')
        tracker.step(1.0)
        tracker.complete_stage(0)
        tracker.begin_stage(1, 'profile chain')
    l, r, _ = apply_audio_profile(l, r, AUDIO_SR, profile, glow, advanced,
                                  on_step=(lambda name, frac: tracker.step(frac)) if tracker else None)
    if tracker:
        tracker.complete_stage(1)
        tracker.begin_stage(2, 'mastering')
        tracker.step(0.5)
        tracker.complete_stage(2)
        tracker.begin_stage(3, 'write')
        tracker.step(0.5)
    out_path = unique_output_path(out_path)
    write_wav_stereo(out_path, l, r)
    if tracker:
        tracker.complete_stage(3)
        tracker.finish()
    return out_path


# ================================================================== video
def _probe_video(path):
    cmd = ['ffprobe', '-v', 'error', '-select_streams', 'v:0',
           '-show_entries', 'stream=width,height,r_frame_rate,nb_frames:format=duration',
           '-of', 'json', path]
    import json
    try:
        info = json.loads(subprocess.run(cmd, capture_output=True, timeout=60).stdout)
        st = info['streams'][0]
        w, h = int(st['width']), int(st['height'])
        num, den = st['r_frame_rate'].split('/')
        fps = float(num) / float(den or 1)
        dur = float(info.get('format', {}).get('duration', 0) or 0)
        n = int(st.get('nb_frames', 0) or 0)
        if n <= 0 and dur > 0:
            n = int(dur * fps)
        return w, h, fps, dur, n
    except Exception:
        return None


def _has_audio_stream(path):
    cmd = ['ffprobe', '-v', 'error', '-select_streams', 'a:0', '-show_entries',
           'stream=codec_type', '-of', 'csv=p=0', path]
    return bool(subprocess.run(cmd, capture_output=True, timeout=30).stdout.strip())


def spatial_energy_map(audio_l, audio_r, sr, n_frames, fps):
    """stereo → per-frame glow direction. horizontal from L/R balance,
    vertical from spectral centroid (bright → up). returns (n,2) float32 in -1..1"""
    n = len(audio_l)
    per_frame = max(1, int(sr / fps))
    maps = np.zeros((n_frames, 2), np.float32)
    n_fft = 1024
    win = np.hanning(n_fft).astype(np.float32)
    for f in range(n_frames):
        s = f * per_frame
        seg_l = audio_l[s:s + per_frame]
        seg_r = audio_r[s:s + per_frame]
        if len(seg_l) < 64:
            break
        el = float(np.mean(seg_l ** 2))
        er = float(np.mean(seg_r ** 2))
        bal = (el - er) / (el + er + 1e-9)          # -1 left .. +1 right
        seg = audio_l[s:s + per_frame] + audio_r[s:s + per_frame]
        if len(seg) >= n_fft:
            S = np.abs(np.fft.rfft(seg[:n_fft] * win))
            freqs = np.fft.rfftfreq(n_fft, 1.0 / sr)
            cent = float(np.sum(S * freqs) / (np.sum(S) + 1e-9))
            vert = np.clip((cent - 1400.0) / 2600.0, -1, 1)   # bright → up
        else:
            vert = 0.0
        maps[f, 0] = bal
        maps[f, 1] = vert
    # smooth so the glow floats, not jitters
    k = 9
    ker = np.hanning(k).astype(np.float32)
    ker /= ker.sum()
    for c in range(2):
        maps[:, c] = np.convolve(np.pad(maps[:, c], k // 2, mode='edge'), ker, 'valid')[:n_frames]
    return maps


def apply_spatial_shift(bloom, dx, dy, max_shift):
    """shift the bloom layer — the spatial glow law"""
    h, w = bloom.shape[:2]
    m = np.float32([[1, 0, dx * max_shift * w * 0.5], [0, 1, dy * max_shift * h * 0.5]])
    return _f32(cv2.warpAffine(bloom, m, (w, h), flags=cv2.INTER_LINEAR,
                               borderMode=cv2.BORDER_REFLECT))


def _ffmpeg_vsync_args():
    v = _ffmpeg_version_tuple()
    if v and v >= (5, 0):
        return ['-fps_mode', 'vfr']
    return ['-vsync', 'vfr']


def _ffmpeg_version_tuple():
    try:
        out = subprocess.run(['ffmpeg', '-version'], capture_output=True, timeout=20).stdout.decode('utf-8', 'ignore')
        for part in out.split():
            if part[0:1].isdigit() and '.' in part:
                return tuple(int(x) for x in part.split('.')[:2])
    except Exception:
        pass
    return None


def process_video_file(inp, out_path, palette='electric', glow=1.0, threshold=0.12,
                       env=1.0, audio_profile=None, spatial_glow=True, neon_audio=False,
                       advanced_audio=None, tracker=None, tmpdir=None):
    """real-time piped neonification: read raw frames → process → pipe to x264.
    audio: neonified with video (neon_audio) via audio_profile, spatial glow
    driven by the original stereo energy."""
    probe = _probe_video(inp)
    if not probe:
        raise RuntimeError(f"cannot probe video: {inp}")
    w, h, fps, dur, n_frames = probe
    n_frames = max(1, n_frames)
    if w % 2 or h % 2:
        w, h = w - (w % 2), h - (h % 2)
    tmp = tmpdir or tempfile.mkdtemp(prefix='neonify_')

    has_audio = _has_audio_stream(inp)
    will_neon_audio = bool(has_audio and neon_audio and audio_profile)
    stages = ['analyze'] + (['neon audio'] if will_neon_audio else []) + ['neonify frames', 'compile']
    if tracker:
        tracker.set_stages(stages)
        tracker.begin_stage(0, 'analyze')
        tracker.step(0.6)

    # ---- audio side ----
    audio_wav = None
    spatial_maps = None
    if has_audio:
        al, ar = decode_audio_stereo(inp)
        if al is not None:
            if tracker:
                tracker.step(0.9)
            if spatial_glow:
                spatial_maps = spatial_energy_map(al, ar, AUDIO_SR, n_frames, fps)
            if will_neon_audio:
                if tracker:
                    tracker.begin_stage(1, 'neon audio')
                al, ar, _ = apply_audio_profile(
                    al, ar, AUDIO_SR, audio_profile, glow, advanced_audio,
                    on_step=(lambda name, frac: tracker.step(frac)) if tracker else None)
                if tracker:
                    tracker.complete_stage(1)
            audio_wav = os.path.join(tmp, 'neon_audio.wav')
            write_wav_stereo(audio_wav, al, ar)
    if tracker and tracker.current < 1:
        tracker.complete_stage(0)

    # ---- frame pipes ----
    rd = subprocess.Popen(
        ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-i', inp,
         '-f', 'rawvideo', '-pix_fmt', 'bgr24', '-v', 'error', '-'],
        stdout=subprocess.PIPE, bufsize=w * h * 3)
    vf = _ffmpeg_vsync_args()
    raw_out = os.path.join(tmp, 'neon_out.mp4')
    cmd = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y',
           '-f', 'rawvideo', '-pix_fmt', 'bgr24', '-s', f'{w}x{h}', '-r', f'{fps:.5f}', '-i', '-']
    if audio_wav:
        cmd += ['-i', audio_wav]
    cmd += ['-c:v', 'libx264', '-preset', 'medium', '-crf', '18', '-pix_fmt', 'yuv420p']
    if audio_wav:
        cmd += ['-c:a', 'aac', '-b:a', '192k', '-shortest']
    cmd += vf + ['-progress', 'pipe:1', raw_out]
    wr = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL)

    frame_stage = 2 if will_neon_audio else 1
    if tracker:
        tracker.begin_stage(frame_stage, 'neonify frames')
    frame_no = 0
    frame_bytes = w * h * 3
    try:
        while True:
            buf = rd.stdout.read(frame_bytes)
            if not buf or len(buf) < frame_bytes:
                break
            frame = np.frombuffer(buf, np.uint8).reshape(h, w, 3)
            edges, _ = edge_field(frame, glow, threshold, env)
            field = neon_glow_stack(frame, edges, glow=glow, env=env, threshold=threshold)
            if spatial_maps is not None and frame_no < len(spatial_maps):
                dx, dy = spatial_maps[frame_no]
                wide = gaussian_blur(edges, 9.0)
                wide = apply_spatial_shift(wide, dx, dy, 0.22)
                field = _f32(np.clip(field + wide * 0.4 * float(np.clip(glow, 0.2, 2.5)), 0, 1.0))
            out = colorize(field, palette)
            wr.stdin.write(out.astype(np.uint8).tobytes())
            frame_no += 1
            if tracker and frame_no % 5 == 0:
                tracker.step(frame_no / n_frames)
    finally:
        rd.stdout.close()
        rd.wait()
        wr.stdin.close()
        if tracker:
            tracker.complete_stage(frame_stage)
        # drain progress while ffmpeg finalizes (real-time compile %)
        if tracker:
            tracker.begin_stage(len(stages) - 1, 'compile')
            try:
                for raw in wr.stdout:
                    line = raw.decode('ascii', 'ignore').strip()
                    if line.startswith('out_time_ms='):
                        try:
                            ms = int(line.split('=')[1])
                            if dur > 0:
                                tracker.step(min(1.0, (ms / 1e6) / dur))
                        except ValueError:
                            pass
                wr.stdout.close()
            except Exception:
                pass
        wr.wait()

    if os.path.isfile(raw_out) and os.path.getsize(raw_out) > 0:
        out_path = unique_output_path(out_path)
        shutil.move(raw_out, out_path)
    else:
        raise RuntimeError(f"video encode failed: {inp}")
    if tracker:
        tracker.complete_stage(len(stages) - 1)
        tracker.finish()
    if not tmpdir:
        shutil.rmtree(tmp, ignore_errors=True)
    return out_path, frame_no


# ================================================================== 3D
# real geometry: mesh → neon wireframe render (no fake shaders). images get
# REMESHED into displaced relief grids. turntable = real rotation.

def save_mesh_obj(path, verts, faces):
    """export a mesh as OBJ — trimesh when present, plain writer otherwise"""
    verts = np.asarray(verts, np.float32)
    faces = np.asarray(faces, np.int64)
    try:
        import trimesh
        m = trimesh.Trimesh(vertices=verts, faces=faces, process=False)
        m.export(path)
        return
    except Exception:
        pass
    lines = []
    for v in verts:
        lines.append(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}")
    for f in faces:
        lines.append(f"f {f[0] + 1} {f[1] + 1} {f[2] + 1}")
    with open(path, 'w') as fh:
        fh.write('\n'.join(lines) + '\n')


def load_mesh(path):
    """OBJ (v/f), PLY (ascii), STL (ascii/binary) → (verts float32 Nx3, faces Mx3)"""
    try:
        import trimesh
        m = trimesh.load(path, force='mesh', process=False)
        if m is not None and len(m.vertices) >= 3 and len(m.faces) >= 1:
            return (np.ascontiguousarray(m.vertices, np.float32),
                    np.ascontiguousarray(m.faces, np.int64))
    except Exception:
        pass
    ext = os.path.splitext(path)[1].lower()
    verts, faces = [], []
    if ext == '.obj':
        with open(path, 'r', errors='ignore') as f:
            for line in f:
                if line.startswith('v '):
                    p = line.split()
                    verts.append([float(p[1]), float(p[2]), float(p[3])])
                elif line.startswith('f '):
                    p = line.split()[1:]
                    idx = [int(x.split('/')[0]) - 1 for x in p]
                    for k in range(1, len(idx) - 1):
                        faces.append([idx[0], idx[k], idx[k + 1]])
    elif ext == '.stl':
        with open(path, 'rb') as f:
            head = f.read(5)
        f.seek(0)
        if head == b'solid' and _looks_ascii(path):
            with open(path, 'r', errors='ignore') as fh:
                tri = []
                for line in fh:
                    p = line.split()
                    if p and p[0] == 'vertex':
                        tri.append([float(p[1]), float(p[2]), float(p[3])])
                        if len(tri) == 3:
                            base = len(verts)
                            verts.extend(tri)
                            faces.append([base, base + 1, base + 2])
                            tri = []
        else:
            import struct
            f.seek(80)
            (n,) = struct.unpack('<I', f.read(4))
            for _ in range(min(n, 2_000_000)):
                data = f.read(50)
                if len(data) < 50:
                    break
                vals = struct.unpack('<12fH', data)
                base = len(verts)
                verts.append([vals[3], vals[4], vals[5]])
                verts.append([vals[6], vals[7], vals[8]])
                verts.append([vals[9], vals[10], vals[11]])
                faces.append([base, base + 1, base + 2])
    elif ext == '.ply':
        with open(path, 'rb') as f:
            header = b''
            while b'end_header' not in header:
                header += f.read(1)
                if len(header) > 65536:
                    raise RuntimeError('bad ply')
            lines = header.decode('ascii', 'ignore').splitlines()
            nv = nf = 0
            for ln in lines:
                if ln.startswith('element vertex'):
                    nv = int(ln.split()[-1])
                elif ln.startswith('element face'):
                    nf = int(ln.split()[-1])
            for _ in range(nv):
                p = f.readline().split()
                verts.append([float(p[0]), float(p[1]), float(p[2])])
            for _ in range(nf):
                p = f.readline().split()
                idx = [int(x) for x in p[1:1 + int(p[0])]]
                for k in range(1, len(idx) - 1):
                    faces.append([idx[0], idx[k], idx[k + 1]])
    else:
        raise RuntimeError(f"unsupported mesh: {path}")
    if not verts or not faces:
        raise RuntimeError(f"empty mesh: {path}")
    return np.asarray(verts, np.float32), np.asarray(faces, np.int64)


def _looks_ascii(path):
    with open(path, 'rb') as f:
        chunk = f.read(1024)
    try:
        chunk.decode('ascii')
        return True
    except UnicodeDecodeError:
        return False


def mesh_edges(faces):
    """unique edges from faces"""
    e = set()
    for tri in faces:
        a, b, c = int(tri[0]), int(tri[1]), int(tri[2])
        e.add((min(a, b), max(a, b)))
        e.add((min(b, c), max(b, c)))
        e.add((min(c, a), max(c, a)))
    return np.asarray(sorted(e), np.int64)


def image_relief_mesh(img_bgr, grid=110, depth=0.85):
    """image → displaced grid mesh (REAL remesh). returns verts, faces.
    heightmap is graded (smoothed) so strokes become flowing terrain, not spikes"""
    lum = luminance(img_bgr)
    h_gt, w_gt = img_bgr.shape[:2]
    gh = max(10, int(grid * h_gt / w_gt))
    lum = cv2.resize(lum, (grid, gh), interpolation=cv2.INTER_AREA)
    lum = cv2.GaussianBlur(lum, (0, 0), max(1.2, grid * 0.02))
    h, w = lum.shape
    z = (lum / 255.0 - 0.5) * 2.0 * depth
    xs = np.linspace(-w / max(h, 1), w / max(h, 1), w, dtype=np.float32)
    ys = np.linspace(-1.0, 1.0, h, dtype=np.float32)
    gx, gy = np.meshgrid(xs, ys)
    verts = np.stack([gx.ravel(), gy.ravel(), z.ravel()], axis=1).astype(np.float32)
    faces = []
    for r in range(h - 1):
        for c in range(w - 1):
            i0 = r * w + c
            faces.append([i0, i0 + 1, i0 + w])
            faces.append([i0 + 1, i0 + w + 1, i0 + w])
    return verts, np.asarray(faces, np.int64)


def normalize_mesh(verts):
    lo, hi = verts.min(axis=0), verts.max(axis=0)
    center = (lo + hi) / 2
    scale = float(np.max(hi - lo)) or 1.0
    return (verts - center) / scale


def rot_y(v, ang):
    c, s = math.cos(ang), math.sin(ang)
    r = np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]], np.float32)
    return v @ r.T


def rot_x(v, ang):
    c, s = math.cos(ang), math.sin(ang)
    r = np.array([[1, 0, 0], [0, c, -s], [0, s, c]], np.float32)
    return v @ r.T


def render_neon_mesh(verts, faces, W=960, H=720, yaw=0.0, pitch=0.2,
                     palette='electric', glow=1.0, fov=1.4):
    """software neon renderer: depth-sorted edges, glow-stacked, palette-tinted"""
    v = rot_x(rot_y(verts, yaw), pitch)
    z = v[:, 2] + 2.6
    px = (v[:, 0] / (z * 0.45)) * W * 0.5 * fov + W / 2
    py = (-v[:, 1] / (z * 0.45)) * H * 0.5 * fov + H / 2
    edges = mesh_edges(faces)

    canvas = np.zeros((H, W), np.float32)
    order = np.argsort(-z[edges].mean(axis=1))  # far → near
    depth_norm = (z[edges].mean(axis=1) - z.min()) / max(1e-5, z.max() - z.min())
    fade = (1.0 - 0.72 * depth_norm).astype(np.float32)

    pts = np.stack([px, py], axis=1).astype(np.float32)
    for ei in order:
        a, b = edges[ei]
        p1, p2 = pts[a], pts[b]
        inten = float(np.clip(fade[ei] * 1.05, 0.05, 1.1))
        cv2.line(canvas, (int(p1[0]), int(p1[1])), (int(p2[0]), int(p2[1])),
                 inten, 1, cv2.LINE_AA)

    field = neon_glow_stack(None, canvas, glow=glow)
    return colorize(field, palette)


def neonize_mesh_file(inp, out_path, palette='electric', glow=1.0,
                      turntable=0, azimuth=30.0, elevation=20.0, fps=30, tracker=None):
    verts, faces = load_mesh(inp)
    verts = normalize_mesh(verts)
    if tracker:
        tracker.set_stages(['load', 'render', 'write'])
        tracker.begin_stage(0, 'load')
        tracker.step(1.0)
        tracker.complete_stage(0)
        tracker.begin_stage(1, 'render')
    out_path = unique_output_path(out_path)

    if turntable > 0:
        frames_dir = None
        total = turntable
        ok = _render_turntable_pipe(out_path, verts, faces, total, azimuth, elevation,
                                    palette, glow, fps,
                                    on_progress=(lambda f: tracker.step(f / total)) if tracker else None)
        if not ok:
            raise RuntimeError('turntable encode failed')
    else:
        img = render_neon_mesh(verts, faces, yaw=math.radians(azimuth),
                               pitch=math.radians(elevation), palette=palette, glow=glow)
        cv2.imwrite(out_path, img, [cv2.IMWRITE_PNG_COMPRESSION, 6])
    if tracker:
        tracker.complete_stage(1)
        tracker.begin_stage(2, 'write')
        tracker.step(1.0)
        tracker.complete_stage(2)
        tracker.finish()
    return out_path


def neonize_relief_file(inp, out_path, palette='electric', glow=1.0, depth=0.85,
                        turntable=48, azimuth=30.0, elevation=35.0, fps=24, tracker=None,
                        export_mesh=False):
    img = cv2.imread(inp, cv2.IMREAD_COLOR)
    if img is None:
        raise RuntimeError(f"cannot read image: {inp}")
    verts, faces = image_relief_mesh(img, depth=depth)
    verts = normalize_mesh(verts)
    if tracker:
        tracker.set_stages(['remesh', 'render', 'write'])
        tracker.begin_stage(0, 'remesh')
        tracker.step(1.0)
        tracker.complete_stage(0)
        tracker.begin_stage(1, 'render')
    out_path = unique_output_path(out_path)
    if turntable > 0:
        total = turntable
        ok = _render_turntable_pipe(out_path, verts, faces, total, azimuth, elevation,
                                    palette, glow, fps,
                                    on_progress=(lambda f: tracker.step(f / total)) if tracker else None)
        if not ok:
            raise RuntimeError('relief turntable encode failed')
    else:
        img2 = render_neon_mesh(verts, faces, yaw=math.radians(azimuth),
                                pitch=math.radians(elevation), palette=palette, glow=glow)
        cv2.imwrite(out_path, img2)
    if tracker:
        tracker.complete_stage(1)
        tracker.begin_stage(2, 'write')
        tracker.step(1.0)
        if export_mesh:
            save_mesh_obj(os.path.splitext(out_path)[0] + '_mesh.obj', verts, faces)
        tracker.complete_stage(2)
        tracker.finish()
    elif export_mesh:
        save_mesh_obj(os.path.splitext(out_path)[0] + '_mesh.obj', verts, faces)
    return out_path


def _render_turntable_pipe(out_path, verts, faces, n_frames, azimuth, elevation,
                           palette, glow, fps, W=960, H=720, on_progress=None):
    """render turntable frames straight into an ffmpeg pipe (real-time %)"""
    cmd = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-y',
           '-f', 'rawvideo', '-pix_fmt', 'bgr24', '-s', f'{W}x{H}', '-r', str(fps), '-i', '-',
           '-c:v', 'libx264', '-preset', 'medium', '-crf', '18', '-pix_fmt', 'yuv420p',
           '-progress', 'pipe:1', out_path]
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    base_pitch = math.radians(elevation)
    base_yaw = math.radians(azimuth)
    try:
        for i in range(n_frames):
            yaw = base_yaw + 2 * math.pi * i / max(1, n_frames)
            frame = render_neon_mesh(verts, faces, W, H, yaw=yaw, pitch=base_pitch,
                                     palette=palette, glow=glow)
            proc.stdin.write(frame.astype(np.uint8).tobytes())
            if on_progress:
                on_progress(i + 1)
        proc.stdin.close()
        proc.wait()
    except BrokenPipeError:
        proc.wait()
        return False
    return proc.returncode == 0


# ================================================================== env
def check_env():
    """CPU-native honesty check. no GPU theater — this tool never needed one.
    ffmpeg is the only optional accelerator user, and it self-detects."""
    print(f"{NEON_DIM}environment{NEON_RESET}")
    print(f"  python      {sys.version.split()[0]}")
    print(f"  numpy       {np.__version__}")
    print(f"  opencv      {cv2.__version__ if cv2 else 'MISSING (image/video disabled)'}")
    ff = shutil.which('ffmpeg')
    print(f"  ffmpeg      {ff or 'not found (video/audio disabled — install ffmpeg)'}")
    cores = os.cpu_count() or 1
    print(f"  cpu cores   {cores}  \u00b7  device: CPU (native \u2014 no GPU required)")
    print()
    return ff is not None


# ================================================================== advanced audio
AUDIO_ADVANCED_SCHEMA = {
    'fire': [('drive', 'heat drive amount', 0.0, 1.0, 0.52),
             ('flicker_rate', 'flicker rate (hz)', 1.0, 12.0, 5.5),
             ('flicker_depth', 'flicker depth', 0.0, 0.6, 0.25),
             ('crackle', 'crackle level', 0.0, 1.5, 0.5),
             ('rumble', 'rumble level', 0.0, 1.5, 0.6),
             ('rumble_hz', 'rumble cutoff (hz)', 40.0, 160.0, 90.0)],
    'ice': [('shimmer_mix', 'shimmer mix', 0.0, 1.0, 0.4),
            ('breath_rate', 'breath rate (hz)', 0.1, 2.0, 0.5),
            ('breath_depth', 'breath depth (ms)', 0.0, 10.0, 3.5),
            ('time', 'frost echo time (s)', 0.05, 1.0, 0.19),
            ('fb', 'frost echo feedback', 0.0, 0.9, 0.3),
            ('damp', 'frost damping (hz)', 1000.0, 12000.0, 7000.0),
            ('mix', 'frost echo mix', 0.0, 1.0, 0.28)],
    'robotic': [('ring_hz', 'ring modulator (hz)', 40.0, 220.0, 88.0),
                ('ring_mix', 'ring mix', 0.0, 1.0, 0.6),
                ('comb', 'formant comb depth', 0.0, 1.0, 0.45),
                ('bits', 'crush bits', 6.0, 16.0, 10.0)],
    'ghost': [('fog_mix', 'fog mix', 0.0, 1.0, 0.5),
              ('whisper_rate', 'whisper rate (hz)', 0.1, 2.0, 0.37),
              ('whisper_depth', 'whisper depth (ms)', 0.0, 12.0, 5.0),
              ('time', 'far echo time (s)', 0.05, 1.5, 0.42),
              ('fb', 'far echo feedback', 0.0, 0.9, 0.42),
              ('damp', 'far damping (hz)', 500.0, 8000.0, 2600.0),
              ('mix', 'far echo mix', 0.0, 1.0, 0.3)],
    'void': [('pitch_mix', 'descent mix', 0.0, 1.0, 0.45),
             ('mix', 'abyss reverb mix', 0.0, 1.0, 0.5),
             ('tone', 'abyss tone (hz)', 500.0, 6000.0, 1500.0),
             ('time', 'cave echo time (s)', 0.05, 1.5, 0.55),
             ('fb', 'cave feedback', 0.0, 0.9, 0.5),
             ('damp', 'cave damping (hz)', 500.0, 8000.0, 1800.0)],
    'echo': [('time', 'echo time (s)', 0.05, 1.5, 0.31),
             ('fb', 'feedback', 0.0, 0.9, 0.45),
             ('damp', 'damping (hz)', 500.0, 12000.0, 4200.0),
             ('mix', 'echo mix', 0.0, 1.0, 0.35),
             ('lp', 'master lowpass (hz)', 1000.0, 16000.0, 9000.0)],
    'slash': [('gain', 'sweep gain', 0.0, 2.0, 0.55)],
}


def ask_advanced_audio(profile):
    """interactive advanced settings walk-through, step by step"""
    schema = AUDIO_ADVANCED_SCHEMA.get(profile, [])
    if not schema:
        return None
    print(f"\n{NEON_DIM}advanced audio settings for '{profile}' (enter = keep default){NEON_RESET}")
    vals = {}
    for key, label, lo, hi, default in schema:
        shown = f"{default:g}"
        raw = input(f"  {label} [{shown}] > ").strip()
        if not raw:
            continue
        try:
            v = float(raw)
            if lo <= v <= hi:
                vals[key] = v if key != 'bits' else int(v)
            else:
                print(f"    {NEON_DIM}out of range {lo}\u2013{hi}, keeping default{NEON_RESET}")
        except ValueError:
            print(f"    {NEON_DIM}not a number, keeping default{NEON_RESET}")
    return vals or None


def ask_audio_profile(default='slash'):
    print(f"\n{NEON_BOLD}audio profile{NEON_RESET}")
    names = AUDIO_PROFILES
    for i, n in enumerate(names, 1):
        mark = ' \u2190 default' if n == default else ''
        print(f"  {i}) {n:<8} {NEON_DIM}{PROFILE_DESCRIPTIONS[n]}{NEON_RESET}{mark}")
    raw = input(f"  pick 1-{len(names)} or name [default: {default}] > ").strip().lower()
    if not raw:
        return default
    if raw in names:
        return raw
    if raw.isdigit() and 1 <= int(raw) <= len(names):
        return names[int(raw) - 1]
    print(f"  {NEON_DIM}unknown profile, using {default}{NEON_RESET}")
    return default


# ================================================================== flows
def _out_default(inp, tag, ext=None):
    """name_neonify_effect_timestamp — collisions die, files self-describe"""
    root = os.path.splitext(os.path.basename(inp))[0]
    ext = ext or os.path.splitext(inp)[1].lower()
    return f"{root}_neonify_{tag}{timestamp_suffix()}{ext}"


def results_dir():
    """every default output lands in ./results so inputs never mix with outputs"""
    d = os.path.join(os.getcwd(), 'results')
    ensure_dir(d)
    return d


def _results_path(name):
    return os.path.join(results_dir(), name)


def _fallback_out(inp, tag, ext, opts):
    """results/ by default, next to the input when asked"""
    name = _out_default(inp, tag, ext)
    if opts.get('next_to_input'):
        return os.path.join(os.path.dirname(os.path.abspath(inp)), name)
    return _results_path(name)


def _unique_or_default(explicit, fallback):
    return unique_output_path(explicit) if explicit else fallback


def flow_image(inputs, opts, tracker):
    outs = []
    for inp in inputs:
        print(f"{NEON_BLUE}\u25b6{NEON_RESET} {os.path.basename(inp)}")
        fallback = _fallback_out(inp, opts['palette'], None, opts)
        out = _unique_or_default(opts.get('output'), fallback)
        path, aux = neonize_image_file(inp, out, opts['palette'], opts['glow'],
                                       opts['threshold'], opts.get('env', 1.0), tracker)
        print(f"{NEON_DIM}  \u2192 {path}   (noise {aux.get('noise', 0):.1f}, pre-blur {aux.get('pre_sigma', 0):.2f}){NEON_RESET}")
        outs.append(path)
    return outs


def flow_video(inputs, opts, tracker):
    outs = []
    for inp in inputs:
        print(f"{NEON_BLUE}\u25b6{NEON_RESET} {os.path.basename(inp)}")
        fallback = _fallback_out(inp, opts['palette'], '.mp4', opts)
        out = _unique_or_default(opts.get('output'), fallback)
        path, n = process_video_file(inp, out, opts['palette'], opts['glow'], opts['threshold'],
                                     opts.get('env', 1.0), audio_profile=opts.get('audio_profile'),
                                     spatial_glow=opts.get('spatial', True),
                                     neon_audio=opts.get('neon_audio', False),
                                     advanced_audio=opts.get('advanced_audio'), tracker=tracker)
        print(f"{NEON_DIM}  \u2192 {path}  ({n} frames){NEON_RESET}")
        outs.append(path)
    return outs


def flow_audio(inputs, opts, tracker):
    outs = []
    for inp in inputs:
        print(f"{NEON_BLUE}\u25b6{NEON_RESET} {os.path.basename(inp)}")
        fallback = _fallback_out(inp, opts.get('audio_profile') or 'slash', '.wav', opts)
        out = _unique_or_default(opts.get('output'), fallback)
        path = neonize_audio_file(inp, out, opts.get('audio_profile') or 'slash', opts['glow'],
                                  opts.get('advanced_audio'), tracker)
        print(f"{NEON_DIM}  \u2192 {path}{NEON_RESET}")
        outs.append(path)
    return outs


def flow_mesh(inputs, opts, tracker):
    outs = []
    for inp in inputs:
        print(f"{NEON_BLUE}\u25b6{NEON_RESET} {os.path.basename(inp)}")
        ext = '.mp4' if opts.get('turntable') else '.png'
        fallback = _fallback_out(inp, f"{opts['palette']}3d", ext, opts)
        out = _unique_or_default(opts.get('output'), fallback)
        relief = opts.get('relief') or os.path.splitext(inp)[1].lower() not in MESH_EXTS
        if relief:
            path = neonize_relief_file(inp, out, opts['palette'], opts['glow'],
                                       opts.get('depth', 0.85), opts.get('turntable', 48),
                                       opts.get('azimuth', 30.0), opts.get('elevation', 20.0),
                                       tracker=tracker, export_mesh=opts.get('export_mesh', False))
        else:
            path = neonize_mesh_file(inp, out, opts['palette'], opts['glow'],
                                     opts.get('turntable', 0), opts.get('azimuth', 30.0),
                                     opts.get('elevation', 20.0), tracker=tracker)
        print(f"{NEON_DIM}  \u2192 {path}{NEON_RESET}")
        outs.append(path)
    return outs


def interactive_mode():
    print_banner()
    ffmpeg_ok = check_env()
    while True:
        print(f"{NEON_BOLD}what are we neonifying?{NEON_RESET}")
        print(f"  {NEON_BLUE}1{NEON_RESET}) image")
        print(f"  {NEON_BLUE}2{NEON_RESET}) video")
        print(f"  {NEON_BLUE}3{NEON_RESET}) audio")
        print(f"  {NEON_BLUE}4{NEON_RESET}) 3d mesh / relief")
        print(f"  {NEON_BLUE}5{NEON_RESET}) batch folder")
        print(f"  {NEON_BLUE}q{NEON_RESET}) quit")
        choice = input(f"{NEON_PINK}>{NEON_RESET} ").strip().lower()
        if choice in ('q', 'quit', 'exit'):
            return 0
        if choice == '1':
            _interactive_single('image', IMG_EXTS, ffmpeg_ok)
        elif choice == '2':
            _interactive_single('video', VID_EXTS, ffmpeg_ok)
        elif choice == '3':
            _interactive_single('audio', AUD_EXTS, ffmpeg_ok)
        elif choice == '4':
            _interactive_single('mesh/relief', MESH_EXTS | IMG_EXTS, ffmpeg_ok)
        elif choice == '5':
            _interactive_batch(ffmpeg_ok)
        else:
            print(f"{NEON_DIM}try 1-5 or q{NEON_RESET}")


def _ask_palette():
    print(f"\n{NEON_BOLD}palette{NEON_RESET}")
    for i, p in enumerate(PALETTE_NAMES, 1):
        print(f"  {i}) {p}")
    raw = input("  pick [1] > ").strip()
    if raw.isdigit() and 1 <= int(raw) <= len(PALETTE_NAMES):
        return PALETTE_NAMES[int(raw) - 1]
    if raw in PALETTE_NAMES:
        return raw
    return 'electric'


def _ask_float(label, default, lo, hi):
    raw = input(f"  {label} [{default:g}] > ").strip()
    if not raw:
        return default
    try:
        v = float(raw)
        return v if lo <= v <= hi else default
    except ValueError:
        return default


def _interactive_single(kind, exts, ffmpeg_ok):
    if not ffmpeg_ok and kind in ('video', 'audio'):
        print(f"{NEON_DIM}ffmpeg missing \u2014 {kind} needs it. install ffmpeg first.{NEON_RESET}")
        return
    raw = input(f"  input {kind} file(s), comma separated > ").strip().strip('"\'')
    if not raw:
        return
    inputs = collect_inputs([p.strip().strip('"\'') for p in raw.split(',')], exts)
    if not inputs:
        print(f"{NEON_DIM}no valid {kind} files found{NEON_RESET}")
        return
    opts = _collect_common_opts(kind)
    tracker = StageTracker()
    try:
        dispatch = {'image': flow_image, 'video': flow_video, 'audio': flow_audio,
                    'mesh/relief': flow_mesh}
        dispatch[kind](inputs, opts, tracker)
    except Exception as e:
        print(f"{NEON_PINK}error: {e}{NEON_RESET}")


def _collect_common_opts(kind):
    opts = {'palette': _ask_palette(),
            'glow': _ask_float('glow', 1.0, 0.1, 3.0),
            'threshold': _ask_float('edge threshold', 0.12, 0.02, 0.5),
            'env': _ask_float('ambient detail', 1.0, 0.0, 2.0)}
    if kind == 'video':
        opts['spatial'] = input("  spatial glow (stereo \u2192 direction) [Y/n] > ").strip().lower() != 'n'
        na = input("  neonify the audio too? [y/N] > ").strip().lower() == 'y'
        opts['neon_audio'] = na
        if na:
            prof = ask_audio_profile()
            opts['audio_profile'] = prof
            if input("  advanced audio settings? [y/N] > ").strip().lower() == 'y':
                opts['advanced_audio'] = ask_advanced_audio(prof)
    elif kind == 'audio':
        prof = ask_audio_profile()
        opts['audio_profile'] = prof
        if input("  advanced audio settings? [y/N] > ").strip().lower() == 'y':
            opts['advanced_audio'] = ask_advanced_audio(prof)
    elif kind == 'mesh/relief':
        opts['turntable'] = int(_ask_float('turntable frames (0 = single image)', 48, 0, 600))
        opts['azimuth'] = _ask_float('azimuth', 30.0, 0.0, 360.0)
        opts['elevation'] = _ask_float('elevation', 20.0, -89.0, 89.0)
        opts['depth'] = _ask_float('relief depth', 0.85, 0.1, 3.0)
    return opts


def _interactive_batch(ffmpeg_ok):
    folder = input("  folder > ").strip().strip('"\'')
    if not os.path.isdir(folder):
        print(f"{NEON_DIM}not a folder{NEON_RESET}")
        return
    kinds = IMG_EXTS | VID_EXTS | AUD_EXTS
    files = collect_inputs([folder], kinds)
    if not files:
        print(f"{NEON_DIM}no media files in folder{NEON_RESET}")
        return
    mode = 'image' if all(is_media_file(f, IMG_EXTS) for f in files) else 'auto'
    opts = _collect_common_opts('image' if mode == 'image' else 'video')
    tracker = StageTracker()
    try:
        imgs = collect_inputs([folder], IMG_EXTS)
        vids = collect_inputs([folder], VID_EXTS)
        auds = collect_inputs([folder], AUD_EXTS)
        if opts.get('palette') is None:
            return
        if imgs:
            flow_image(imgs, opts, tracker)
        if ffmpeg_ok and vids:
            flow_video(vids, opts, tracker)
        if ffmpeg_ok and auds:
            opts.setdefault('audio_profile', 'slash')
            flow_audio(auds, opts, tracker)
    except Exception as e:
        print(f"{NEON_PINK}error: {e}{NEON_RESET}")


def run_gui():
    """FIXED launcher: resolves gui.py next to this file OR frozen bundle dir.
    the old 'gui.py not found' bug came from resolving against cwd."""
    here = os.path.dirname(os.path.abspath(__file__))
    candidates = [os.path.join(here, 'gui.py')]
    base = getattr(sys, '_MEIPASS', None)
    if base:
        candidates.append(os.path.join(base, 'gui.py'))
    exe_dir = os.path.dirname(os.path.abspath(sys.executable)) if getattr(sys, 'frozen', False) else None
    if exe_dir:
        candidates.append(os.path.join(exe_dir, 'gui.py'))
    gui_py = next((c for c in candidates if os.path.isfile(c)), None)
    if not gui_py:
        print("Error: gui.py not found. looked next to neonify.py and in the bundle.")
        return 1
    sys.path.insert(0, os.path.dirname(gui_py))
    import importlib.util
    spec = importlib.util.spec_from_file_location('neonify_gui', gui_py)
    mod = importlib.util.module_from_spec(spec)
    sys.modules['neonify_gui'] = mod
    try:
        spec.loader.exec_module(mod)
        return 0
    except ImportError as e:
        print(f"GUI needs PyQt5 ({e}). pip install PyQt5  \u2014 or use the CLI.")
        return 1


def list_profiles():
    print(f"{NEON_BOLD}audio profiles{NEON_RESET}")
    for n in AUDIO_PROFILES:
        print(f"  {n:<8} {PROFILE_DESCRIPTIONS[n]}")
    print(f"\n{NEON_DIM}every profile responds to --glow differently \u2014 they are not one settings sheet{NEON_RESET}")


# ================================================================== main
def build_parser():
    p = argparse.ArgumentParser(
        prog='neonify',
        description=f"{APP_NAME} \u2014 {APP_TAG} (cpu-native, no gpu needed)")
    p.add_argument('command', nargs='?', default='gui',
                   choices=['gui', 'image', 'video', 'audio', 'mesh', 'batch', 'profiles'],
                   help="what to do (default: gui)")
    p.add_argument('input', nargs='*', help="input file(s) or folder")
    p.add_argument('-o', '--output', help="output path (default: auto _timestamp name)")
    p.add_argument('--next-to-input', action='store_true',
                   help="save outputs next to the input file instead of results/")
    p.add_argument('--palette', choices=PALETTE_NAMES, default='electric')
    p.add_argument('--glow', type=float, default=1.0, help="glow intensity 0.1-3.0")
    p.add_argument('--threshold', type=float, default=0.12, help="edge threshold 0.02-0.5")
    p.add_argument('--env', type=float, default=1.0, help="ambient detail 0-2")
    p.add_argument('--profile', choices=AUDIO_PROFILES, default=None,
                   help="audio profile (audio mode, or video with --neon-audio)")
    p.add_argument('--advanced-audio', default=None,
                   help="advanced audio overrides json, e.g. '{\"drive\":0.8}'")
    p.add_argument('--neon-audio', action='store_true', help="neonify audio with the video")
    p.add_argument('--no-spatial', action='store_true', help="disable spatial glow (stereo pan)")
    p.add_argument('--turntable', type=int, default=0, help="3d turntable frames (mesh mode)")
    p.add_argument('--azimuth', type=float, default=30.0)
    p.add_argument('--elevation', type=float, default=20.0)
    p.add_argument('--depth', type=float, default=0.85, help="relief depth (image\u2192mesh)")
    p.add_argument('--export-mesh', action='store_true',
                   help="relief runs also export the remeshed geometry as .obj")
    p.add_argument('--hwaccel', action='store_true',
                   help="let ffmpeg use hardware accel if present (decode only; optional)")
    p.add_argument('--json-progress', action='store_true',
                   help="emit progress as JSON lines (for the GUI)")
    return p


def _attach_parent_console():
    """windowed exe: re-attach to the cmd that launched `neonify.exe cli`
    double-click (no parent console) skips silently, GUI stays clean"""
    if os.name != 'nt' or not getattr(sys, 'frozen', False):
        return
    if len(sys.argv) < 2 or sys.argv[1] in ('gui', '--help', '-h'):
        return
    try:
        import ctypes
        k32 = ctypes.windll.kernel32
        if k32.GetConsoleWindow():
            return
        if not k32.AttachConsole(-1):
            return
        sys.stdout = open('CONOUT$', 'w', buffering=1, encoding='utf-8', errors='replace')
        sys.stderr = open('CONOUT$', 'w', buffering=1, encoding='utf-8', errors='replace')
        sys.stdin = open('CONIN$', 'r', encoding='utf-8', errors='replace')
    except Exception:
        pass


def main(argv=None):
    if argv is None:
        argv = sys.argv[1:]
    _attach_parent_console()
    args = build_parser().parse_args(argv)
    if args.command == 'profiles':
        print_banner()
        list_profiles()
        return 0
    if args.command == 'gui':
        return run_gui()

    if not args.input:
        if args.command in ('image', 'video', 'audio', 'mesh', 'batch'):
            print("no input given \u2014 entering interactive mode\n")
            return interactive_mode()
        print_banner()
        return interactive_mode()

    if not cv2:
        print("opencv missing: pip install opencv-python")
        return 1

    advanced = None
    if args.advanced_audio:
        try:
            advanced = json.loads(args.advanced_audio)
        except Exception:
            print("--advanced-audio must be valid json")
            return 2

    opts = {'palette': args.palette, 'glow': args.glow, 'threshold': args.threshold,
            'env': args.env, 'output': args.output, 'next_to_input': args.next_to_input,
            'spatial': not args.no_spatial,
            'neon_audio': args.neon_audio, 'audio_profile': args.profile,
            'advanced_audio': advanced, 'turntable': args.turntable,
            'azimuth': args.azimuth, 'elevation': args.elevation, 'depth': args.depth,
            'export_mesh': args.export_mesh}
    if args.command == 'batch':
        files = collect_inputs(args.input, IMG_EXTS | VID_EXTS | AUD_EXTS)
    elif args.command == 'image':
        files = collect_inputs(args.input, IMG_EXTS)
    elif args.command == 'video':
        files = collect_inputs(args.input, VID_EXTS)
    elif args.command == 'audio':
        files = collect_inputs(args.input, AUD_EXTS)
    else:
        files = collect_inputs(args.input, MESH_EXTS | IMG_EXTS)
    if not files:
        print("no valid input files found")
        return 1

    print_banner()
    ffmpeg_ok = check_env()
    if args.command in ('video', 'audio') and not ffmpeg_ok:
        print("ffmpeg required for video/audio. install it, then retry.")
        return 1
    if args.command == 'video' and args.neon_audio and not args.profile:
        opts['audio_profile'] = 'slash'

    tracker = StageTracker(json_mode=args.json_progress)
    dispatch = {'image': flow_image, 'video': flow_video, 'audio': flow_audio,
                'mesh': flow_mesh,
                'batch': lambda i, o, t: (flow_image(collect_inputs(i, IMG_EXTS), o, t),
                                          flow_video(collect_inputs(i, VID_EXTS), o, t) if ffmpeg_ok else None,
                                          (opts.__setitem__('audio_profile', opts.get('audio_profile') or 'slash'),
                                           flow_audio(collect_inputs(i, AUD_EXTS), o, t)) if ffmpeg_ok else None)}
    try:
        dispatch[args.command](args.input, opts, tracker)
    except KeyboardInterrupt:
        print(f"\n{NEON_DIM}interrupted{NEON_RESET}")
        return 130
    except Exception as e:
        print(f"{NEON_PINK}error: {e}{NEON_RESET}")
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
