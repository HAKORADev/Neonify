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
| `install.bat` | Creates desktop shortcut, Start Menu entry, and adds Neonify to PATH |

> After running `install.bat`, you can open a **new** command prompt and use `neonify` from anywhere.

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
└── install.sh / install.bat ← shortcut & alias installer
```

---

## 🛠️ CLI Examples

```bash
# Images and videos
neonify neon photo.jpg
neonify neon clip.mp4 --pulse on
neonify neon art.png --palette synthwave --glow 1.6

# Audio
neonify audio song.mp3
neonify audio song.mp3 --anim

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
