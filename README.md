# Neonify — Procedural Neon Art Tool

<p align="center">
  <img src="src/logo.png" alt="Neonify Logo" width="128" height="128"/>
</p>

<p align="center">
  <a href="https://github.com/HAKORADev/Neonify/releases">
    <img src="https://img.shields.io/badge/%F0%9F%93%A6%20Release-v0.5.0-FF4466?style=for-the-badge" alt="Latest Release"/>
  </a>
</p>

<p align="center">
  <strong>Local &bull; Free &bull; Cross-Platform &bull; Zero AI</strong><br/>
  Turn images, videos, audio and 3D meshes into glowing neon art with pure math.
</p>

---

**Neonify** is a local, free, offline procedural art engine that transforms whatever asset you feed it into neon art. Edge detection finds the lines, Gaussian bloom pyramids make them glow, palette mapping gives them color, and audio gets rebuilt through a chain of neon sound effects — no neural networks, no model downloads, no accounts. The entire engine is deterministic math running on your machine, and it uses your CUDA GPU automatically when one is present (falling back to full CPU when it is not).

📦 **Pre-built binaries available** — no Python or setup needed. Grab the [latest release](https://github.com/HAKORADev/Neonify/releases) with CPU binaries for Windows and Linux, download, extract, and run.

---

## Quick Start

### Run from Source
```bash
# Clone the repository
git clone https://github.com/HAKORADev/Neonify.git
cd Neonify

# Install PyTorch first (CPU build is enough — GPU is used automatically if you install a CUDA build)
pip install torch

# Install the rest of the dependencies
pip install -r requirements.txt

# Launch GUI
python src/neonify.py

# Or use CLI mode
python src/neonify.py cli
```

### Installation Requirements
```bash
# Install FFmpeg (required for video and audio inputs)
# Windows: winget install FFmpeg
# macOS: brew install ffmpeg
# Linux: sudo apt install ffmpeg
```

---

## The Four Canvases

| Command | Input | Output | Engine |
|---------|-------|--------|--------|
| **neon** | Image | `_neon` image | Sobel edges → multi-octave bloom → palette mapping |
| **neon** | Video | `_neon` video | Per-frame neon engine + audio-reactive glow pulse (STFT beat envelope) |
| **audio** | Audio | `_neon.wav` | Real neon sound: tube drive, ping-pong echoes, void reverb, pulse tremolo, shimmer vibrato, slash sweeps |
| **mesh** | `.obj` / `.stl` | `_neon.png` | Wireframe extraction → perspective projection → depth-weighted glow (Fresnel-style rim) |
| **mesh --turntable** | `.obj` / `.stl` | `_neon_turntable.mp4` | Full orbit animation of the glowing wireframe |

### 🎨 **7 Neon Palettes — for pixels and for sound**

| Palette | Look | Sound |
|---------|------|-------|
| **Electric** (default) | Deep blue → cyan → white, classic neon sign | Bright, punchy, crisp echoes |
| **Synthwave** | Purple → pink → sunset orange | Warm tape echoes, slow sunset wobble |
| **Toxic** | Radioactive green → lime | Acid bite, fast tremolo, sharp slashes |
| **Ice** | Frozen blue → pure white | Clean, wide, long crystalline void |
| **Fire** | Ember red → orange → white heat | Hot drive, heavy sub rumble |
| **Ghost** | Monochrome white — the dark&white signature | Haunted dark void, distant muffled echoes |
| **Spectrum** | Hue mapped to edge direction — every angle gets its own color | Everything at once, widest stage |

---

## Usage Guide

### GUI Mode

1. Launch: `python src/neonify.py`
2. Drag & drop files (images, videos, audio and meshes can be mixed)
3. Pick palette, glow and edge threshold
4. Click **NEONIFY**
5. Preview results — images/videos with the before/after comparison slider, audio with the waveform view and a play button
6. Open the output folder — every result lands next to its source with a `_neon` name

### CLI Mode (Interactive)
```bash
python src/neonify.py cli
```

### CLI Mode (Direct)
```bash
# Images and videos
python src/neonify.py neon photo.jpg
python src/neonify.py neon clip.mp4 --pulse on
python src/neonify.py neon art.png --palette synthwave --glow 1.6 -o out.png
python src/neonify.py neon art.png --threshold 0.06

# Audio
python src/neonify.py audio song.mp3
python src/neonify.py audio song.mp3 --palette ghost

# 3D meshes
python src/neonify.py mesh model.obj
python src/neonify.py mesh model.stl --turntable 120 --azimuth 45

# System info
python src/neonify.py info
```

### Options

| Flag | Values | Default | Description |
|------|--------|---------|-------------|
| `--palette` | electric, synthwave, toxic, ice, fire, ghost, spectrum | electric | Neon color palette |
| `--glow` | 0.2 – 3.0 | 1.0 | Bloom intensity multiplier |
| `--threshold` | 0.02 – 0.5 | 0.12 | Edge detection sensitivity |
| `--pulse` | auto, on, off | auto | Audio-reactive glow pulse for videos |
| `--turntable` | frames | 0 | Mesh: orbit video with N frames |
| `--azimuth` / `--elevation` | degrees | 30 / 20 | Mesh camera angles for single views |
| `--device` | auto, cpu, gpu | auto | Compute device (GPU falls back to CPU) |
| `-o` | path | auto | Output file or folder |

---

## 🔊 Neon Audio

`neonify audio track.mp3` does not draw the sound — it **re-synthesizes it**. The output is a real neonized audio file (`track_neon.wav`, stereo 44.1 kHz) built from a pure-math DSP chain:

- **Tube glow drive** — asymmetric tanh saturation, like a neon sign buzzing to life
- **Ping-pong echoes** — feedback delay lines bouncing left ↔ right, darkening per bounce
- **Void reverb** — a deep, dark multi-tap space under the track
- **Pulse tremolo + shimmer vibrato** — the glow literally breathes through the amplitude and pitch
- **Slash sweeps** — two resonant sweeps slicing across the spectrum (STFT-domain filter)
- **Wide neon stage** — mid/side widening, sub rumble under the bass, air shelf on top, soft-clip limiter

Every palette doubles as a sound profile and the Glow setting drives the FX intensity — so the same track can go haunted (`ghost`), acid (`toxic`), frozen (`ice`) or rumbling (`fire`) with one flag.

---

## How It Works

No AI anywhere — every pixel is earned with math:

- **Edges**: Sobel operators on a pre-smoothed luminance field, soft-thresholded into tube lines
- **Glow**: a cascade of wide Gaussian blurs at four octaves, additive-composited into a bloom field
- **Color**: intensity-mapped palette LUTs with a white-hot core pass for the tube centers
- **Audio**: pure numpy DSP — FFT convolutions for filters, multi-tap delay networks for echo and void, overlap-add STFT for the slash sweeps, tanh waveshaping for the tube drive
- **Pulse**: the video's own audio track is decoded and its STFT envelope modulates the bloom octaves per frame — the glow literally breathes with the beat
- **Meshes**: OBJ/STL parsing, deduplicated edge extraction, orbit camera with perspective projection, depth-weighted line intensity for the rim-glow look

---

## System Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| **CPU** | 2 cores | 4+ cores |
| **RAM** | 4GB | 8GB+ |
| **GPU** | None (CPU works) | NVIDIA GTX 1060+ (used automatically) |
| **Storage** | 1GB | SSD recommended |
| **FFmpeg** | Required for video/audio | — |

**Note:** Neonify works fully on CPU. GPU with CUDA accelerates large images and long videos automatically when PyTorch sees it.

---

## Supported Formats

### Images
`.jpg`, `.jpeg`, `.png`, `.bmp`, `.tiff`, `.tif`, `.webp`

### Videos
`.mp4`, `.avi`, `.mov`, `.mkv`, `.webm`, `.flv`, `.wmv`, `.m4v`

### Audio
`.mp3`, `.wav`, `.flac`, `.ogg`, `.m4a`, `.aac`, `.wma`, `.opus`

### Meshes
`.obj`, `.stl` (binary and ASCII)

---

## 🎬 Showcase

<p align="center">
  <i>Samples will land here — the folder is ready and waiting.</i>
</p>

---

## License

Released under the [MIT License](LICENSE).
