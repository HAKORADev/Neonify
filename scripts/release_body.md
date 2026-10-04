Neonify turns images, videos, audio and 3D meshes into neon art. It runs locally on your machine.

## Packages

| File | What it is |
|------|------------|
| `neonify-windows-cpp-v0.5.0.zip` | Windows app. Extract, double-click `neonify.exe` for the GUI or `cli.bat` for the interactive CLI. `neonify-desktop.exe` is the live desktop neonifier |
| `neonify-linux-cpp-v0.5.0.tar.gz` | Linux app. Extract, `./neonify` for the GUI, `./neonify cli` for the interactive CLI, `./neonify-desktop` for the desktop neonifier |

ffmpeg is not in the package. It is needed for video and audio only — install it once with `winget install FFmpeg` (Windows) or `sudo apt install ffmpeg` (Linux).

## In this build

- One exe per binary on Windows — no DLLs, no folders next to it, icon included
- The engine math is the original tube law, ported 1:1: `/4` sobel, the four-scale glow stack, the original palette ramps (electric, synthwave, toxic, ice, fire, ghost, spectrum), the core added after the palette
- The original can sit under the effect three ways: wiped to black, kept with neon only on the edges, or kept under the full global glow
- Desktop neonifier: `ctrl+alt+n` neonifies your screen live, `ctrl+alt+n` then `w` neonifies just the focused window. DXGI desktop duplication + d3d11 compute on Windows; X11 capture, or the desktop portal on Wayland, on Linux. Click-through overlay, tray control
- First run probes the machine once — graphics device, opencl, a real cpu/gpu benchmark of the neon ops, the video encoders ffmpeg can actually drive here — and writes `neonify.ini` next to the binary. Every value carries two comment lines (what it is, valid range) and a checker repairs corrupt or extra entries without wiping the file. Delete it for a fresh detection
- The engine math runs on the GPU only when the benchmark proved the GPU is faster on this machine; the ini records what was actually detected
- GUI: inputs on the left (preview / delete per file), results on the right (preview / compare, separate clear-previews and clear-results buttons), previews in the center — images and video with the zoom widget, `ctrl+wheel` or `ctrl+up/down` zooming at the pointer, eased panning, zoom ceiling scales with the media (a 4000x4000 shot reaches 4000%), video player with seek / speed / full time, audio waveform player
- Advanced audio settings are schema-driven per profile — every parameter of the selected profile, no profile picker inside
- CLI: give it files or a folder, it detects what is inside (images / videos / audio / 3d) and asks per type; output location is a three-way choice — results folder, next to each input, or a custom path
- 3D: OBJ / PLY / STL as neon wireframes, still or turntable, verified against generated spheres, tori, beveled boxes and twisted prisms
- Outputs land in `results/` named `name_neonify_effect_timestamp` — nothing ever overwrites
- Non-ASCII file paths work end to end; PNG transparency follows the source art

## Notes

- ffmpeg is used for video and audio decoding/encoding; hardware encoding is used when the first-run probe proves the machine has it
- The Python source under `src/python` is frozen — the native build is the product
