# neonify — future

thoughts worth keeping, written down so they survive the sessions.

## where this is going

neonify is becoming its own standalone product. not a plugin, not an add-on for
another tool — a self-contained procedural neon engine with a desktop presence.
the desktop neonifier is the first step in that direction: the same math that
processes a file now lives on the screen, in real time, over anything.

## the reshade-shaped hole

the overlay today captures frames, neonifies them and paints them back. a real
in-process mod (the reshade/enb way) hooks the game's own frame and draws before
present — zero capture latency, frame-perfect sync. that needs per-game
injection and is its own project; the capture route was chosen because it is
universal (every game, every app, every emulator) with no anti-cheat surface.
the long-term idea: keep the universal overlay, and if injection ever happens
it becomes an optional extra, not the base.

## cartoonify

the natural sibling of the neon look. same edge field, but the composite step
renders cel-shaded flat fills instead of glow ramps — quantized colors, ink
outlines, hard light bands. as a real 3D pass (on meshes, not just post) it
turns a model into a comic-shaded object with neon rim light. worth building as
a second output mode once the keep-original compositing law proves itself.

## latency

the overlay targets the capture-present chain under 20ms: duplication gives the
frame in single-digit ms, the d3d11 compute pass adds ~1-2ms, the readback and
paint a few more. lower than that needs the in-process hook above. the tray
tooltip shows the real measured number, not a spec sheet.

## gpu truth

detection runs once, writes what it actually found into neonify.ini, and every
later run re-verifies. no invented numbers: if the bench says the cpu wins, the
cpu wins; if opencl refuses, the math runs on the cpu and the file says so.

## naming

outputs are `{name}_neonify_{effect}_{timestamp}.{ext}` — every file describes
itself: which effect made it, when. audio carries the real applied profile tag.
