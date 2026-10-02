# Neonify — Procedural Neon Art Tool

<p align="center">
  <img src="src/assets/logo.png" alt="Neonify Logo" width="128" height="128"/>
</p>

<p align="center">
  <a href="https://github.com/HAKORADev/Neonify/releases">
    <img src="https://img.shields.io/badge/%F0%9F%93%A6%20Release-v0.5.0-FF4466?style=for-the-badge" alt="Latest Release"/>
  </a>
</p>

<p align="center">
  <strong>Local &bull; Free &bull; Cross-Platform &bull; Zero AI &bull; Zero GPU</strong><br/>
  Turn images, videos, audio and 3D meshes into glowing neon art with pure math.
</p>

---

**Neonify** is a local, free, offline procedural art engine that transforms whatever asset you feed it into neon art. Sobel edge detection finds the lines, a four-octave Gaussian bloom pyramid makes them glow, palette mapping gives them color, audio gets rebuilt through a chain of neon sound effects — no neural networks, no model downloads, no accounts, and **no GPU anywhere in the pipeline**. The entire engine is deterministic math running on your CPU, and a normal 2014+ desktop CPU is already more than enough for it.

📦 **Pre-built native binaries available** — grab the [latest release](https://github.com/HAKORADev/Neonify/releases): a single native executable per platform (built with Qt 5.15 — the same toolkit IMDER uses), no Python, no PyInstaller, no setup. A full Python implementation of the same engine ships alongside for tinkerers.

---

## Quick Start

### Native Executable (recommended)
```bash
# Download the release zip, extract, then:
./neonify                    # launches the Qt GUI
./neonify image photo.jpg    # CLI mode — see below
```
The native binary needs **ffmpeg** on your PATH for video/audio inputs (images, meshes and relief work without it). Install it once and you are done:
```bash
# Windows: winget install FFmpeg
# macOS:   brew install ffmpeg
# Linux:   sudo apt install ffmpeg
```

### Run the Python Engine from Source
```bash
git clone https://github.com/HAKORADev/Neonify.git
cd Neonify
pip install -r requirements.txt   # numpy + opencv + PyQt5. that's all. no torch.
python src/python/neonify.py      # GUI
python src/python/neonify.py image photo.jpg   # CLI
```

### Build the Native Engine from Source
```bash
# needs: cmake >= 3.16, a C++17 compiler, Qt 5.15 and OpenCV 4 dev packages
sudo apt install qtbase5-dev libopencv-dev cmake   # linux example
cmake -S src/cpp -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/neonify
```

---

## The Canvases

| Command | Input | Output | Engine |
|---------|-------|--------|--------|
| **image** | Image | `name_neon_YYMMDDHHMMSS.png` | Sobel tube edges → 4-octave bloom → palette LUT |
| **video** | Video | `name_neon_YYMMDDHHMMSS.mp4` | Per-frame neon engine, piped live into x264 with a real-time compile percentage |
| **audio** | Audio | `name_neon_YYMMDDHHMMSS.wav` | Full DSP re-synthesis through neon sound profiles |
| **mesh** | `.obj` / `.ply` / `.stl` | static or `--turntable N` mp4 | True wireframe remesh → perspective projection → depth-weighted neon lines |
| **mesh** | Image (`.png`, `.jpg`, …) | relief turntable mp4 | The image is **genuinely re-meshed**: graded heightmap → triangulated 3D relief → neon wireframe orbit render |
| **batch** | Folder | everything above | Mixed batches — images, videos, audio and meshes sorted automatically |

### Text survives. Low-quality video survives.

Two hard problems were engineered explicitly, not left to defaults:

- **Text preservation** — noise-adaptive pre-blur, an adaptive edge threshold, a gradient-energy support gate and density calming keep thin strokes as clean tubes instead of blobbing them into halos. Small UI text, subtitles and hand lettering stay readable through the neon pass.
- **Noisy / compressed b&w video** — the noise level is estimated per frame (median absolute deviation of the Laplacian), pre-blur and threshold scale with it, and glow halos are unstacked from hot cores so walls of static do not white out the frame. Long-video consistency was verified end to end.

---

## 🎨 7 Neon Palettes — for pixels and for sound

| Palette | Look | Sound character |
|---------|------|-----------------|
| **Electric** (default) | Deep blue → cyan → white, classic neon sign | Bright, punchy, crisp echoes |
| **Crimson** | Deep red → hot orange core | Warm bite, aggressive drive |
| **Ice** | Frozen blue → pure white | Clean, wide, long crystalline void |
| **Toxic** | Radioactive green → lime | Acid bite, fast tremolo |
| **Violet** | Purple dusk → electric magenta | Dreamy detune, wide stage |
| **Golden** | Amber → white gold | Fat mid warmth, heavy rumble |
| **Ghost** | Monochrome white — the dark&white signature | Haunted fog, distant muffled echoes |

---

## 🔊 Audio Profiles (not just shared settings)

Audio is treated as its own instrument: pick a **profile**, then optionally open **advanced audio settings** and override every parameter of the chain interactively (GUI dialog or CLI prompt). Glow drives the intensity; the source melody never gets ruined — every profile is mixed against the dry signal.

| Profile | Character |
|---------|-----------|
| **slash** (default) | The signature diagonal energy sweep |
| **fire** | Burn — tube drive, crackle, sub rumble, flicker tremolo synced to the glow |
| **ice** | Ice-steam — octave shimmer, airy shelf, glassy breath, frost echoes |
| **robotic** | Metal ring-modulation, formant combs, bit-crushed edges |
| **ghost** | Fog — breathing dark reverb, whisper detune drift |
| **void** | The abyss — octave-down descent, huge dark space |
| **echo** | Proper clean ping-pong echo, tone-shaped per bounce |

```bash
# profile only
neonify audio song.mp3 --profile ghost
# profile + advanced overrides (see every parameter with: neonify profiles)
neonify audio song.mp3 --profile fire --advanced-audio '{"drive":0.7,"rumble_hz":120}'
```

---

## ✨ Spatial Glow & Neon Audio for Video

- **Spatial glow** — the video's stereo field is analysed frame-by-frame: balance pans the bloom left/right, spectral centroid lifts it up/down. A sound slamming on the right channel literally makes the right side of the frame glow harder. Disable with `--no-spatial`.
- **`--neon-audio`** — neonify the video's audio in the same pass with any profile + advanced settings. The output mp4 carries the processed soundtrack instead of the original one.

---

## Usage Guide

### GUI Mode
1. Launch `neonify` (native) or `python src/python/neonify.py` — the GUI now starts reliably on every platform and install layout
2. Drag & drop files — images, videos, audio and meshes can be mixed
3. Pick palette, glow, edge threshold and ambient detail
4. Choose an audio profile for video/audio jobs, or open **Advanced audio settings…** for per-parameter control
5. Click **NEONIFY** — progress shows n/n completed stages with a live % inside the current one
6. Results preview in-app; audio/video/text previews included

### CLI Mode (Direct)
```bash
neonify image photo.jpg --palette violet --glow 1.6
neonify image art.png --threshold 0.06          # more edges
neonify video clip.mp4 --neon-audio --profile fire
neonify audio song.mp3 --profile ice --advanced-audio '{"breath_depth":6}'
neonify mesh model.obj --turntable 120 --azimuth 45
neonify mesh photo.png --turntable 90            # true relief remesh from a flat image
neonify batch ./my_media/
neonify profiles                                 # list profiles + every advanced parameter
neonify --help
```

### Options

| Flag | Values | Default | Description |
|------|--------|---------|-------------|
| `--palette` | electric, crimson, ice, toxic, violet, golden, ghost | electric | Neon color palette |
| `--glow` | 0.1 – 3.0 | 1.0 | Bloom intensity multiplier |
| `--threshold` | 0.02 – 0.5 | 0.12 | Edge detection sensitivity |
| `--env` | 0 – 2 | 1.0 | Ambient detail adaptation |
| `--profile` | fire, ice, robotic, ghost, void, echo, slash | slash | Audio profile |
| `--advanced-audio` | JSON | — | Per-parameter profile overrides |
| `--neon-audio` | flag | off | Neonify audio with the video |
| `--no-spatial` | flag | off | Disable stereo spatial glow |
| `--turntable` | frames | 0 | 3D orbit video with N frames |
| `--azimuth` / `--elevation` | degrees | 30 / 20 | 3D camera angles |
| `--depth` | 0 – 2 | 0.85 | Relief depth (image → mesh) |
| `--hwaccel` | flag | off | Prefer GPU-accelerated ffmpeg decode when ffmpeg offers it |
| `--json-progress` | flag | off | Machine-readable progress lines |
| `-o` | path | auto | Output path (auto = never overwrites) |

The only step in the whole tool that *may* touch a GPU is ffmpeg's own optional hardware decoding (`--hwaccel`) — and even that is a convenience flag, never a requirement. The engine itself is 100% CPU math. There is no OpenCV-CUDA, no shaders, no PyTorch — custom per-GPU builds were rejected on purpose so one binary runs everywhere.

---

## The Output Naming Law

Outputs are **never overwritten** and never get boring `(1)`/`(2)` copies. Every result lands next to its source as:

```
name_neon_261002182032.png     ← _YYMMDDHHMMSS, VODER-style
```

Re-run the same input ten times and you keep ten timestamped versions.

---

## How It Works

No AI anywhere — every pixel is earned with math:

- **Edges**: Sobel operators on a pre-smoothed luminance field, soft-thresholded into tube lines; MAD noise estimation drives adaptive pre-blur and thresholds
- **Glow**: a cascade of wide Gaussian blurs at four octaves (σ = 2, 6, 16, 38), additive-composited into a bloom field with halo unstacking
- **Color**: intensity-mapped palette LUTs with a white-hot core pass for the tube centers
- **3D**: OBJ/PLY/STL parsing, deduplicated edge extraction, perspective orbit camera, depth-weighted line intensity — and for flat images a graded heightmap (Gaussian-smoothed before displacement, so no spike forests) triangulated into a real relief mesh rendered through the same neon line engine
- **Audio**: pure DSP — FFT convolutions for filters, multi-tap delay networks, overlap-add STFT sweeps, ring modulation, formant combs, tanh tube drive, all chained per profile and driven by the glow level
- **Video**: decoded frames stream raw into an x264 pipe with `-progress` parsing so the compile percentage is real, not a fake crawl

### The native engine
The C++ port is a **1:1 port** of the Python engine — same tube laws, same σ pyramid, same DSP chains, same timestamp naming. One executable, Qt 5.15 Widgets GUI (the same Qt IMDER uses) + full CLI, ~30× faster startup, no runtime installs. The CLI banner survives verbatim. 😉

---

## System Requirements

| Component | Minimum | Comfortable |
|-----------|---------|-------------|
| **CPU** | 2 cores | 4+ cores |
| **RAM** | 2GB | 4GB+ |
| **GPU** | **None. Not used. Never needed.** | — |
| **FFmpeg** | needed only for video/audio inputs | — |
| **Storage** | 50MB | SSD if you batch a lot |

A normal 2014+ CPU already clears the recommended bar. 4K stills and long videos simply take proportionally longer — there is no GPU theater here, the honest bottleneck is your processor.

---

## Supported Formats

### Images
`.jpg`, `.jpeg`, `.png`, `.bmp`, `.tiff`, `.tif`, `.webp` — plus images → **3D relief remesh**

### Videos
`.mp4`, `.avi`, `.mov`, `.mkv`, `.webm`, `.gif`

### Audio
`.mp3`, `.wav`, `.flac`, `.ogg`, `.m4a`, `.aac`, `.wma`

### Meshes
`.obj`, `.ply`, `.stl` (binary and ASCII)

---

## 🎬 Showcase

<p align="center">
  <i>Samples will land here — the folder is ready and waiting.</i>
</p>

---

## License

Released under the [MIT License](LICENSE).
