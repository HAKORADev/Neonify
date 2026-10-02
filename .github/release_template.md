## Neonify {{VERSION}} — Desktop Binaries

Pre-built desktop binaries for Linux and Windows. No Python, no pip, no cloning — just download, extract, and glow.

---

## 📦 Downloads

| Platform | File |
|----------|------|
| **Linux** (x86_64) | `neonify-cpu-linux_{{VERSION}}.tar.gz` |
| **Windows** (x86_64) | `neonify-cpu-windows_{{VERSION}}.zip` |

---

## 🚀 Quick Start

### Linux

```bash
# Download and extract
tar xzf neonify-cpu-linux_{{VERSION}}.tar.gz
cd neonify

# Launch GUI
./neonify gui

# Or launch interactive CLI
./neonify cli

# Optional: set up shortcuts & shell alias
./install.sh
```

### Windows

```
1. Download and extract neonify-cpu-windows_{{VERSION}}.zip
2. Double-click neonify.exe to launch the GUI
```

**Handy batch files included:**

| File | What it does |
|------|-------------|
| `neonify.exe` | Main binary — launches GUI (double-click to open) |
| `cli.bat` | Opens interactive CLI mode |
| `cmd.bat` | Opens a command prompt in the Neonify folder |

---

## 🔄 What's New in {{VERSION}}

{{WHATS_NEW}}

---

## 📂 What's Inside

The app icon lives **inside** the binary — the Windows executable carries it (file icon, window and taskbar) and there are no loose logo files to lose.

```
neonify/
├── neonify / neonify.exe ← main binary (GUI + CLI, icon embedded)
├── _internal/            ← Python runtime + all dependencies
├── cli.sh / cli.bat      ← launch interactive CLI mode
├── install.sh            ← desktop shortcut & shell alias (Linux)
└── cmd.bat               ← open a terminal in the Neonify folder (Windows)
```

---

## 🔊 Neon Audio

`neonify audio track.mp3` re-synthesizes the sound itself — the output is a real neonized audio file (`track_neon.wav`, stereo 44.1 kHz), not a picture:

- **Tube glow drive** — warm asymmetric saturation, like a neon sign buzzing to life
- **Ping-pong echoes** — damped echoes bouncing left ↔ right
- **Void reverb** — a deep, dark space under the track
- **Pulse tremolo + shimmer vibrato** — the neon breathing
- **Slash sweeps** — two resonant filter slashes across the spectrum
- **Wide neon stage** — mid/side widening, sub rumble and air shelf

Every palette is also a sound profile — `ghost` is a haunted dark void, `toxic` bites, `ice` shimmers, `fire` rumbles. The Glow setting drives the FX intensity.

---

## 🛠️ CLI Examples

```bash
# Images and videos
neonify neon photo.jpg
neonify neon clip.mp4 --pulse on
neonify neon art.png --palette synthwave --glow 1.6

# Audio
neonify audio song.mp3
neonify audio song.mp3 --palette ghost

# 3D meshes
neonify mesh model.obj --turntable 120

# System info
neonify info
```

---

## ⚡ Notes

- **CPU-only builds**: fully functional on any modern PC. If a CUDA GPU is present when running from source, the engine uses it automatically.
- **FFmpeg** (system install) is required for video and audio inputs: `winget install FFmpeg` (Windows) / `sudo apt install ffmpeg` (Linux).
- No AI models, no downloads, no accounts — the entire engine is deterministic math running on your machine.
