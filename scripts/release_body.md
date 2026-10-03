Neonify turns images, videos, audio and 3D meshes into neon art. It runs locally on your machine.

## Packages

| File | What it is |
|------|------------|
| `neonify-windows-cpp-v0.5.0.zip` | Windows app. Extract, double-click `neonify.exe` for the GUI or `cli.bat` for the interactive CLI |
| `neonify-linux-cpp-v0.5.0.tar.gz` | Linux app. Extract, `./neonify` for the GUI, `./neonify cli` for the interactive CLI |
| `neonify-windows-python-v0.5.0.zip` | The same app from the Python build, Windows |
| `neonify-linux-python-v0.5.0.tar.gz` | The same app from the Python build, Linux |

ffmpeg is not in the zip. It is needed for video and audio only — install it once with `winget install FFmpeg` (Windows) or `sudo apt install ffmpeg` (Linux).

## In this build

- One exe on Windows — no DLLs, no folders next to it, icon included
- The packages contain just the app and its launchers, nothing else
- GUI: inputs on the left (preview / delete per file), results on the right (preview / compare, plus a clear-previews button), previews in the center — image shown fitted in a fixed frame, video preview rendered frame by frame with seek and speed, audio waveform player with current / total time
- Advanced audio settings open with a profile switcher and show every parameter for the selected profile
- Keep-inside option (image / video): the original look is kept inside the detected edges, the tubes and glow stay on the edges
- CLI: give it files or a folder, it detects what is inside (images / videos / audio / 3d) and asks per type; the same prompts and defaults as the Python build
- Outputs land in `results/` next to where you run from, named `name_neonify_effect_timestamp` — `photo_neonify_ice_261003152708.png`. There is a `--next-to-input` option (and a GUI checkbox) to save next to the input file instead
- Non-ASCII file paths work end to end (windows ansi file APIs were the old failure)
- PNG transparency survives: alpha in, tubes on alpha out

## Notes

- Duplicate files are refused with a warning, nothing overwrites
- Everything runs on the CPU; ffmpeg is used for video and audio decoding/encoding only
