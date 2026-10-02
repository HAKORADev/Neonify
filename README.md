# Neonify - Procedural Neon Art Tool

<p align="center">
  <img src="src/assets/logo.png" alt="Neonify Logo" width="128" height="128"/>
</p>

<p align="center">
  <a href="https://github.com/HAKORADev/Neonify/releases">
    <img src="https://img.shields.io/badge/%F0%9F%93%A6%20Release-v0.5.0-FF4466?style=for-the-badge" alt="Latest Release"/>
  </a>
  <a href="https://github.com/HAKORADev/Neonify/blob/main/LICENSE">
    <img src="https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge" alt="License: MIT"/>
  </a>
</p>

<p align="center">
  <strong>Local &bull; Free &bull; Cross-Platform &bull; Procedural</strong><br/>
  Turn images, videos, audio and 3D meshes into glowing neon art with deterministic math.
</p>

---

**Neonify** is a local, offline procedural art engine. Edge detection finds the lines, a four-octave Gaussian bloom pyramid makes them glow, palette mapping colors them, and audio gets rebuilt through a chain of neon sound effects. No neural networks, no model downloads, no accounts. The whole engine is deterministic math running on your CPU — a normal 2014+ desktop is enough.

## Quick Start

### Option 1: Native Build (recommended)

```bash
# Download the release zip, extract, then:
# Windows: double-click neonify.exe (GUI), or run cli.bat (interactive CLI)
# Linux:   ./neonify (GUI), or ./neonify cli (interactive CLI)
```

The native build is a single executable per platform (built with Qt 5.15). On Windows the ffmpeg tool ships in the zip and stays next to the exe; on Linux install it with your package manager.

### Option 2: Run from Source (Python)

```bash
git clone https://github.com/HAKORADev/Neonify.git
cd Neonify
pip install -r requirements.txt
python src/python/neonify.py            # interactive CLI
python src/python/neonify.py gui        # GUI
python src/python/neonify.py image photo.png --palette ice
```

### Installation Requirements

```bash
# FFmpeg (required for video and audio processing)
# Windows: winget install FFmpeg      macOS: brew install ffmpeg      Linux: sudo apt install ffmpeg
```

## Two Ways to Use Neonify

| Feature | Native (C++ / Qt) | Source (Python) |
|---------|-------------------|-----------------|
| **Interface** | Qt 5.15 GUI + interactive CLI | PyQt5 GUI + interactive CLI |
| **Best for** | Plug-and-play, one small exe | Tinkering, automation, scripting |
| **Engine** | Same laws, ported 1:1 | Reference engine |
| **Dependencies** | None (ffmpeg for video/audio) | NumPy, OpenCV, soundfile, trimesh, PyQt5 |

## Core Capabilities

### 🖼️ Images and Videos

| Feature | Description |
|---------|-------------|
| **Tube look** | Sobel ridge detection + four-scale glow stack (σ 2/6/16/38) — the lines read as lit tubes, not sticker outlines |
| **Noise defense** | Direct MAD noise estimate drives pre-blur, adaptive thresholding and support gating, so noisy b&w footage and dark phone shots stay clean |
| **Text preservation** | Halo unstacking + density adaptation keep letters readable — dense text calms the glow instead of becoming a blob |
| **Transparency** | PNG with alpha in, PNG with alpha out — tubes on transparency, no background box |
| **Spatial glow** | Stereo audio pans the glow left/right and up/down while the video plays |
| **Real-time progress** | n/n counts completed stages, % moves inside the current stage, video compile shows true encoder progress |

### 🔊 Audio Profiles

Every profile is an effect chain that reacts to the Glow setting:

| Profile | Sound |
|---------|-------|
| **fire** | burn — crackle, rumble, flicker, heat drive |
| **ice** | ice-steam — octave shimmer, airy shelf, glassy breath |
| **robotic** | metal ring-mod, formant combs, crushed edges |
| **ghost** | fog — breathing dark reverb, whisper detune |
| **void** | the abyss — octave-down, huge dark space |
| **echo** | clean ping-pong echo, tone-shaped |
| **slash** | the signature diagonal energy sweep |

Each profile exposes its own advanced parameters (echo time, damping, ring frequency, crush depth…) — in the CLI as `--advanced-audio` JSON, in the GUI as a per-profile dialog.

### 🧊 3D Meshes and Relief

| Feature | Description |
|---------|-------------|
| **Mesh render** | Loads OBJ / PLY / STL, renders the wireframe as neon tubes |
| **Relief remesh** | Any image becomes a displaced 3D relief grid (graded heightfield, no spike forests) |
| **Turntable** | Real 360° orbit render straight into an mp4 |
| **Export** | `--export-mesh` writes the remeshed geometry back out as OBJ (mesh loading and export go through trimesh) |

### 🖥️ GUI (native build)

- **Left** — input files with per-file Preview and Delete; the file kind is detected natively (image / video / audio / 3D mesh) and duplicates are refused with a warning
- **Center** — preview pane: zoomable image viewer, video player, audio player, and a 10-second 360° orbit render for 3D inputs
- **Right** — results with per-file Preview and a **Compare** button that puts the result next to its original: synced zoom/pan for images, synced seek/playback-speed for videos, position sync for audio
- **Bottom** — settings that adapt to what you queued, all values 0–100 (the app converts internally)

## CLI Examples

```bash
# images and videos
neonify image photo.png --palette ice
neonify video clip.mp4 --neon-audio --profile ghost

# audio
neonify audio track.mp3 --profile fire
neonify audio track.mp3 --advanced-audio '{"time":0.4,"fb":0.5}'

# 3d
neonify mesh model.obj --turntable 240
neonify mesh art.png --export-mesh

# batch (everything in a folder)
neonify batch ./photos

# audio profiles reference
neonify profiles
```

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--palette` | `electric` | `electric`, `crimson`, `ice`, `toxic`, `violet`, `golden`, `ghost` |
| `--glow` | `1.0` | glow intensity (0–2) |
| `--threshold` | `0.12` | edge sensitivity (0–1) |
| `--env` | `1.0` | scene brightness adaptation (0–2) |
| `-o, --output` | auto | output path (default: `results/<input>_<effect>_<timestamp>`) |
| `--profile` | `slash` | audio profile (see table above) |
| `--neon-audio` | off | neonify the audio too (video mode) |
| `--no-spatial` | off | disable stereo spatial glow |
| `--advanced-audio` | — | JSON overrides for profile parameters |
| `--turntable <n>` | `0` | 3d turntable frames (0 = still) |
| `--azimuth / --elevation` | `30 / 20` | 3d view angles |
| `--depth` | `0.85` | relief depth |
| `--export-mesh` | off | relief runs also write the remeshed OBJ |
| `--json-progress` | off | machine-readable progress lines (for tooling) |

## Outputs

Every default output lands in a `results/` folder so inputs never mix with results, and every name carries the effect that produced it plus a second-resolution timestamp — `photo_ice_261002182032.png`, `track_fire_261002182120.wav`, `clip_crimson_261002182251.mp4`. Nothing ever overwrites.

## Requirements

| Component | Minimum | Recommended |
|-----------|---------|-------------|
| **CPU** | 2 cores, 2014+ desktop | 4+ cores |
| **RAM** | 4 GB | 8 GB |
| **OS** | Windows 10+ / Linux | — |
| **FFmpeg** | any recent build (video/audio only) | — |

The engine is 100% deterministic CPU math — no neural networks, no model files, no online calls.

## For AI Agents

See [future.md](future.md) for the project vision and roadmap notes.

## License

MIT — see [LICENSE](LICENSE).
