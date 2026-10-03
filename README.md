# Neonify

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

Neonify turns images, videos, audio and 3D meshes into neon art. It runs locally on your machine, no accounts, no uploads.

Images: edges become glowing tubes, PNG transparency is kept, and an optional keep-inside mode preserves the original look inside the detected edges. Videos: the same treatment per frame, with an option to run the audio through it too. Audio: seven profiles (fire, ice, robotic, ghost, void, echo, slash) that each react to the Glow setting. 3D: OBJ / PLY / STL rendered as neon wireframes, still or as a 360° turntable video, plus image-to-relief remeshing with OBJ export.

## Download

Get a package from the [releases](https://github.com/HAKORADev/Neonify/releases), extract, run:

- `neonify.exe` — opens the GUI (Windows)
- `cli.bat` — interactive CLI (Windows)
- `./neonify` — GUI (Linux), `./neonify cli` — interactive CLI

ffmpeg is needed for video and audio only. Install it once: `winget install FFmpeg` on Windows, `sudo apt install ffmpeg` on Linux.

There is also a Python build per platform for running from source:

```bash
git clone https://github.com/HAKORADev/Neonify.git
cd Neonify
pip install -r requirements.txt
python src/python/neonify.py            # interactive CLI
python src/python/neonify.py gui        # GUI
python src/python/neonify.py image photo.png --palette ice
```

## The GUI

Left: input files, each with Preview and Delete. Right: results, each with Preview and Compare. Center: the preview — zoomable image viewer, video player, audio waveform player, and a 10-second 360° orbit render for 3D inputs. Bottom: settings for the files you queued, on 0–100 scales. Compare shows the result next to its original — shared zoom and pan for images, synced seek and speed for videos, position sync for audio with waveforms.

## CLI

```bash
neonify image photo.png --palette ice
neonify video clip.mp4 --neon-audio --profile ghost
neonify audio track.mp3 --profile fire
neonify mesh model.obj --turntable 240
neonify batch ./photos
neonify profiles
```

Without a command, `neonify` opens the GUI. `neonify cli` opens the interactive CLI.

## Options

| Option | Default | Description |
|--------|---------|-------------|
| `--palette` | `electric` | `electric`, `crimson`, `ice`, `toxic`, `violet`, `golden`, `ghost` |
| `--glow` | `1.0` | glow intensity 0.1–3.0 |
| `--threshold` | `0.12` | edge sensitivity 0.02–0.5 |
| `--env` | `1.0` | ambient detail 0–2 |
| `-o, --output` | auto | output path |
| `--next-to-input` | off | save next to the input file instead of `results/` |
| `--profile` | `slash` | audio profile (audio mode, or video with `--neon-audio`) |
| `--neon-audio` | off | neonify the audio with the video |
| `--no-spatial` | off | disable spatial glow (stereo pan) |
| `--advanced-audio` | — | JSON overrides for profile parameters |
| `--turntable <n>` | `0` | 3d turntable frames (0 = still) |
| `--azimuth / --elevation` | `30 / 20` | 3d view angles |
| `--depth` | `0.85` | relief depth 0.1–3.0 |
| `--export-mesh` | off | relief runs also write the remeshed OBJ |
| `--json-progress` | off | progress as JSON lines (for the GUI) |

## Outputs

Default outputs land in `results/`, named `name_neonify_effect_timestamp` — `photo_neonify_ice_261003152708.png`, `track_neonify_fire_261003153228.wav`, `clip_neonify_crimson_261003154510.mp4`. Use `--next-to-input` (or the GUI checkbox) to save next to the input file instead. Nothing ever overwrites.

## Requirements

Any recent desktop CPU. ffmpeg for video and audio. Windows 10+ or Linux.

## License

MIT — see [LICENSE](LICENSE).
