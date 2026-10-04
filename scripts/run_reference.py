#!/usr/bin/env python3
"""runs the REAL pre-cpp python engine (the reference the owner pointed at) on
test frames and stores its outputs. the harness then compares my port against
these true pixels — no reimplementation in the loop for the reference side."""
import importlib.util
import json
import os
import sys

import numpy as np
import cv2

REF = "/home/z/my-project/neonify_precpp_ref/neonify.py"
spec = importlib.util.spec_from_file_location("neonify_ref", REF)
ref = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ref)

out_dir = "/home/z/my-project/Neonify/build/ref_outputs"
os.makedirs(out_dir, exist_ok=True)

# force cpu, silence the device prints
ref.get_device(force_cpu=True)

frames = {}

flat = np.full((96, 96, 3), 90, np.uint8)
frames["flat"] = flat

img = np.zeros((240, 320, 3), np.uint8)
img[:, :] = (40, 30, 20)
cv2.circle(img, (90, 110), 55, (240, 200, 60), -1)
cv2.rectangle(img, (180, 40), (290, 190), (30, 220, 130), -1)
cv2.line(img, (0, 220), (320, 10), (250, 250, 250), 3)
rng = np.random.default_rng(7)
noise = rng.normal(0, 6, img.shape).astype(np.float32)
img = np.clip(img.astype(np.float32) + noise, 0, 255).astype(np.uint8)
frames["scene"] = img

combos = []
for palette in ['electric', 'synthwave', 'toxic', 'ice', 'fire', 'ghost', 'spectrum']:
    for glow, thr, env in [(1.0, 0.12, 1.0), (0.6, 0.22, 1.6), (1.6, 0.06, 0.5)]:
        combos.append((palette, glow, thr, env))

manifest = {}
for fname, frame in frames.items():
    for palette, glow, thr, env in combos:
        key = f"{fname}_{palette}_{glow}_{thr}_{env}"
        out = ref.process_image_neon(frame.copy(), palette=palette, glow=glow,
                                     threshold=thr, env=env)
        path = os.path.join(out_dir, key + ".png")
        cv2.imwrite(path, out)
        manifest[key] = {"file": path, "frame": fname, "palette": palette,
                         "glow": glow, "threshold": thr, "env": env,
                         "border_max": int(out[:3, :].max()),
                         "center_max": int(out[40:56, 40:56].max() if fname == "flat" else 0)}

with open(os.path.join(out_dir, "manifest.json"), "w") as f:
    json.dump(manifest, f, indent=1)

# the border question, answered by the real engine
flat_electric = manifest["flat_electric_1.0_0.12_1.0"]
print("REAL REF flat frame, electric:")
print("  border rows max:", flat_electric["border_max"])
print("  center max:", flat_electric["center_max"])

scene_key = "scene_electric_1.0_0.12_1.0"
m = manifest[scene_key]
print("REAL REF scene frame, electric: border rows max:", m["border_max"])
print("outputs in", out_dir, "-", len(manifest), "images")
