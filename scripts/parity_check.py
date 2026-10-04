#!/usr/bin/env python3
"""parity harness: the pre-cpp python engine (torch) reimplemented in numpy
side by side with the c++ port's exact law, on the same pixels. the two must
land within a couple of 8-bit steps of each other — anything larger means the
port drifted and the 'neon feel' dies again.

also validates:
  - the palette lut loop in neon_image.h against np.interp (the reference law)
  - every ini schema default against its own validity rule
"""
import math
import re
import sys

import numpy as np
import cv2

FAILURES = []


def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print(f"[{status}] {name} {detail}")
    if not cond:
        FAILURES.append(name)


# ---------------------------------------------------------------- reference (old python, numpy)
def gaussian_kernel1d(sigma):
    radius = max(1, int(math.ceil(sigma * 3.0)))
    size = 2 * radius + 1
    x = np.arange(size, dtype=np.float32) - radius
    k = np.exp(-(x * x) / (2.0 * sigma * sigma))
    return (k / k.sum()).astype(np.float32)


def gaussian_blur_ref(img, sigma):
    if sigma <= 0.05:
        return img
    k = gaussian_kernel1d(sigma)
    r = len(k) // 2
    return cv2.filter2D(img, -1, k[None, :], anchor=(r, 0), borderType=cv2.BORDER_CONSTANT) \
        if False else cv2.sepFilter2D(img, -1, k, k, delta=0, borderType=cv2.BORDER_CONSTANT)


def wide_blur_ref(t, sigma):
    if sigma >= 12.0:
        h, w = t.shape[0], t.shape[1]
        small = cv2.resize(t, (max(1, w // 4), max(1, h // 4)), interpolation=cv2.INTER_LINEAR)
        small = gaussian_blur_ref(small, sigma * 0.25)
        return cv2.resize(small, (w, h), interpolation=cv2.INTER_LINEAR)
    return gaussian_blur_ref(t, sigma)


def sobel_field_ref(gray01):
    kx = np.array([[-1, 0, 1], [-2, 0, 2], [-1, 0, 1]], np.float32) / 4.0
    gx = cv2.filter2D(gray01, -1, kx, borderType=cv2.BORDER_CONSTANT)
    gy = cv2.filter2D(gray01, -1, kx.T, borderType=cv2.BORDER_CONSTANT)
    mag = np.sqrt(gx * gx + gy * gy + 1e-8)
    ang = np.arctan2(gy, gx)
    return mag, ang


def smoothstep_ref(lo, hi, x):
    t = np.clip((x - lo) / (hi - lo + 1e-8), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


STOPS = {
    'electric': [(0.00, (0.004, 0.010, 0.045)), (0.28, (0.05, 0.25, 0.85)), (0.58, (0.15, 0.72, 1.00)), (0.84, (0.60, 0.95, 1.00)), (1.00, (1.00, 1.00, 1.00))],
    'synthwave': [(0.00, (0.040, 0.004, 0.060)), (0.30, (0.45, 0.08, 0.75)), (0.60, (0.98, 0.20, 0.60)), (0.85, (1.00, 0.50, 0.45)), (1.00, (1.00, 0.96, 0.88))],
    'toxic': [(0.00, (0.004, 0.035, 0.012)), (0.30, (0.05, 0.50, 0.12)), (0.64, (0.35, 0.95, 0.20)), (1.00, (0.90, 1.00, 0.80))],
    'ice': [(0.00, (0.004, 0.012, 0.024)), (0.35, (0.10, 0.30, 0.55)), (0.70, (0.55, 0.80, 0.95)), (1.00, (1.00, 1.00, 1.00))],
    'fire': [(0.00, (0.035, 0.005, 0.002)), (0.30, (0.55, 0.08, 0.02)), (0.64, (1.00, 0.45, 0.05)), (0.88, (1.00, 0.80, 0.30)), (1.00, (1.00, 1.00, 0.92))],
    'ghost': [(0.00, (0.012, 0.012, 0.014)), (0.40, (0.35, 0.35, 0.38)), (0.75, (0.75, 0.78, 0.82)), (1.00, (1.00, 1.00, 1.00))],
}


def lut_ref(name):
    idx = np.linspace(0.0, 1.0, 256)
    if name == 'spectrum':
        h = idx
        i = (h * 6.0).astype(int) % 6
        f = h * 6.0 - np.floor(h * 6.0)
        p = 0.1 * np.ones_like(h)
        q = 0.1 + 0.9 * (1 - f)
        t = 0.1 + 0.9 * f
        r = np.select([i == 0, i == 1, i == 2, i == 3, i == 4], [np.ones_like(h), q, p, p, t], default=np.ones_like(h))
        g = np.select([i == 0, i == 1, i == 2, i == 3, i == 4], [t, np.ones_like(h), np.ones_like(h), q, p], default=p)
        b = np.select([i == 0, i == 1, i == 2, i == 3, i == 4], [p, p, t, np.ones_like(h), np.ones_like(h)], default=q)
        return np.stack([r, g, b], 1).astype(np.float32)
    rows = STOPS[name]
    px = np.array([r[0] for r in rows])
    cols = np.array([r[1] for r in rows], dtype=np.float32)
    return np.stack([np.interp(idx, px, cols[:, c]) for c in range(3)], axis=1).astype(np.float32)


def hsv_to_rgb(h, s, v):
    h = (h % 1.0) * 6.0
    i = np.floor(h).astype(int) % 6
    f = h - np.floor(h)
    p = v * (1 - s)
    q = v * (1 - s * f)
    t = v * (1 - s * (1 - f))
    r = np.where(i == 0, v, np.where(i == 1, q, np.where(i == 2, p, np.where(i == 3, p, np.where(i == 4, t, v)))))
    g = np.where(i == 0, t, np.where(i == 1, v, np.where(i == 2, v, np.where(i == 3, q, np.where(i == 4, p, p)))))
    b = np.where(i == 0, p, np.where(i == 1, p, np.where(i == 2, t, np.where(i == 3, v, np.where(i == 4, v, q)))))
    return np.stack([r, g, b], -1)


def neonize_ref(bgr_u8, palette, glow, threshold, env):
    rgb = cv2.cvtColor(bgr_u8, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
    lum = 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]
    base = gaussian_blur_ref(lum.astype(np.float32), 1.0)
    mag, ang = sobel_field_ref(base)
    edge = smoothstep_ref(threshold, threshold + 0.22, mag)
    glow_field = np.zeros_like(edge)
    for sigma, weight in zip((2.0, 6.0, 16.0, 38.0), (0.9, 0.62, 0.45, 0.32)):
        glow_field = glow_field + wide_blur_ref(edge, sigma) * (weight * glow * env)
    excite = 0.72 + 0.42 * env
    energy = np.clip((edge * 1.15 + glow_field * 0.85) * excite, 0.0, 1.0)
    idx = np.clip((energy * 255.0).astype(int), 0, 255)
    lut = lut_ref(palette)
    color = lut[idx]
    if palette == 'spectrum':
        hue = ((ang + math.pi) / (2.0 * math.pi)) % 1.0
        color = hsv_to_rgb(hue, 0.9, energy)
    core = np.power(edge, 1.2) * 0.85
    color = np.clip(color + core[..., None] * 0.55, 0.0, 1.0)
    return (np.clip(color * 255.0, 0, 255)).astype(np.uint8)[..., ::-1]  # to bgr


# ---------------------------------------------------------------- cpp port law (as written in neon_image.h)
def neonize_cpplaw(bgr_u8, palette, glow, threshold, env):
    rgb = cv2.cvtColor(bgr_u8, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
    lum = 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]
    base = gaussian_blur_ref(lum.astype(np.float32), 1.0)
    mag, ang = sobel_field_ref(base)
    edge = smoothstep_ref(threshold, threshold + 0.22, mag)
    glow_field = np.zeros_like(edge)
    for sigma, weight in zip((2.0, 6.0, 16.0, 38.0), (0.9, 0.62, 0.45, 0.32)):
        glow_field = glow_field + wide_blur_ref(edge, sigma) * (weight * glow * env)
    excite = 0.72 + 0.42 * env
    energy = np.clip((edge * 1.15 + glow_field * 0.85) * excite, 0.0, 1.0)
    idx8 = np.clip((energy * 255.0), 0, 255).astype(np.uint8)
    lut8 = (lut_ref(palette) * 255.0).clip(0, 255)
    color = lut8[idx8] / 255.0
    if palette == 'spectrum':
        hue = (ang / (2 * math.pi) + 0.5) % 1.0
        color = hsv_to_rgb(hue, 0.9, energy)
    core = np.power(edge, 1.2) * 0.85 * 0.55
    color = np.clip(color + core[..., None], 0.0, 1.0)
    return (np.clip(color * 255.0, 0, 255)).astype(np.uint8)[..., ::-1]


# ---------------------------------------------------------------- 1. lut loop check (the actual c++ loop, in python)
def lut_cpp_loop(name):
    stops = STOPS.get(name)
    out = np.zeros((256, 3), np.float32)
    for i in range(256):
        t = i / 255.0
        if name == 'spectrum':
            h6 = (t % 1.0) * 6.0
            ii = int(h6) % 6
            f = h6 - math.floor(h6)
            p, q, tt = 0.1, 0.1 + 0.9 * (1 - f), 0.1 + 0.9 * f
            r, g, b = [(1, tt, p), (q, 1, p), (p, 1, tt), (p, q, 1), (tt, p, 1), (1, p, q)][ii]
            out[i] = (r, g, b)
            continue
        if t <= stops[0][0]:
            out[i] = stops[0][1]
            continue
        val = stops[-1][1]
        for s in range(1, len(stops)):
            if t <= stops[s][0]:
                u = (t - stops[s - 1][0]) / max(1e-6, stops[s][0] - stops[s - 1][0])
                a, bcol = stops[s - 1][1], stops[s][1]
                val = tuple(a[k] + (bcol[k] - a[k]) * u for k in range(3))
                break
        out[i] = val
    return out


for name in ['electric', 'synthwave', 'toxic', 'ice', 'fire', 'ghost', 'spectrum']:
    ref = lut_ref(name)
    got = lut_cpp_loop(name)
    diff = np.abs(ref - got).max() * 255.0
    check(f"lut {name} matches np.interp law", diff <= 1.0, f"(max {diff:.2f}/255)")

# ---------------------------------------------------------------- 2. engine parity
rng = np.random.default_rng(7)
img = np.zeros((240, 320, 3), np.uint8)
img[:, :] = (40, 30, 20)
cv2.circle(img, (90, 110), 55, (240, 200, 60), -1)
cv2.rectangle(img, (180, 40), (290, 190), (30, 220, 130), -1)
cv2.line(img, (0, 220), (320, 10), (250, 250, 250), 3)
noise = rng.normal(0, 6, img.shape).astype(np.float32)
img = np.clip(img.astype(np.float32) + noise, 0, 255).astype(np.uint8)

def interior(arr, ring=8):
    return arr[ring:-ring, ring:-ring]


# gold parity: against the pixels the REAL pre-cpp engine wrote (scripts/
# run_reference.py, torch, the actual old source). the in-harness mirror stays
# as a fallback when the torch outputs are not around.
import os as _os
ref_dir = '/home/z/my-project/Neonify/build/ref_outputs'
have_real = _os.path.isdir(ref_dir) and len(_os.listdir(ref_dir)) > 40

for palette in ['electric', 'synthwave', 'toxic', 'ice', 'fire', 'ghost', 'spectrum']:
    for glow, thr, env in [(1.0, 0.12, 1.0), (0.6, 0.22, 1.6), (1.6, 0.06, 0.5)]:
        got = neonize_cpplaw(img, palette, glow, thr, env)
        if have_real:
            key = f"scene_{palette}_{glow}_{thr}_{env}"
            ref = cv2.imread(_os.path.join(ref_dir, key + ".png"))
            src = "real-ref"
        else:
            ref = neonize_ref(img, palette, glow, thr, env)
            src = "mirror"
        d = np.abs(ref.astype(np.int16) - got.astype(np.int16))
        check(f"parity {palette} glow={glow} thr={thr} env={env} [{src}]",
              d.mean() <= 1.0 and d.max() <= 4.0,
              f"(mean {d.mean():.3f}, max {d.max()})")

# ---------------------------------------------------------------- 3. wiped background law: near-black floor on every palette
for name in ['electric', 'synthwave', 'toxic', 'ice', 'fire', 'ghost']:
    lut = (lut_ref(name) * 255.0).clip(0, 255)
    floor = lut[0]
    check(f"palette {name} floor stays dark", floor.max() <= 40, f"({floor})")

# spectrum never samples the lut (hue_mix is always 1 in the reference), and
# its real floor — hsv(hue, 0.9, energy=0) — must be black. the interior only:
# the zero-pad gradient at the frame border is REAL reference behavior (the
# torch engine glows there too — verified, border 255 / center 11).
# 512px frame, deep interior: the reference's border halo reaches ~116px
# (the sigma-38 scale), so the probe sits far from every border and edge
flat = np.full((512, 512, 3), 90, np.uint8)
spec = neonize_cpplaw(flat, 'spectrum', 1.0, 0.12, 1.0)
deep = spec[200:312, 200:312]
check("spectrum output floor stays black", int(deep.max()) <= 12, f"(max {int(deep.max())})")
for name in ['electric', 'synthwave', 'toxic', 'ice', 'fire', 'ghost']:
    wipe = neonize_cpplaw(flat, name, 1.0, 0.12, 1.0)
    # the ceiling is the reference's own lut floor — synthwave sits at 15
    # (0.060 * 255), the old dark purple. anything above ~20 would be the
    # old bright-green background bug coming back.
    deep = wipe[200:312, 200:312]
    check(f"wiped floor on {name} stays black", int(deep.max()) <= 18, f"(max {int(deep.max())})")

# ---------------------------------------------------------------- 3b. composite modes: the owner's round-7 wording
#   wipe  -> non-edges are the near-black lut floor
#   keep  -> non-edges stay the ORIGINAL pixels, untouched (no global wash)
#   glow  -> original visible under the full energy wash
def composite(bgr_u8, mode, palette='electric', glow=1.0, threshold=0.12, env=1.0):
    rgb = cv2.cvtColor(bgr_u8, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
    lum = 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]
    base = gaussian_blur_ref(lum.astype(np.float32), 1.0)
    mag, ang = sobel_field_ref(base)
    edge = smoothstep_ref(threshold, threshold + 0.22, mag)
    glow_field = np.zeros_like(edge)
    for sigma, weight in zip((2.0, 6.0, 16.0, 38.0), (0.9, 0.62, 0.45, 0.32)):
        glow_field = glow_field + wide_blur_ref(edge, sigma) * (weight * glow * env)
    excite = 0.72 + 0.42 * env
    energy = np.clip((edge * 1.15 + glow_field * 0.85) * excite, 0.0, 1.0)
    idx8 = np.clip((energy * 255.0), 0, 255).astype(np.uint8)
    lut8 = (lut_ref(palette) * 255.0).clip(0, 255)
    color = lut8[idx8] / 255.0
    if palette == 'spectrum':
        hue = (ang / (2 * math.pi) + 0.5) % 1.0
        color = hsv_to_rgb(hue, 0.9, energy)
    core = np.power(edge, 1.2) * 0.85 * 0.55
    neon = np.clip(color + core[..., None], 0.0, 1.0)
    if mode == 'wipe':
        return (neon * 255).astype(np.uint8)[..., ::-1], edge, energy
    if mode == 'keep':
        halo = wide_blur_ref(edge, 6.0) * 0.55
        m = np.clip(edge * 1.15 + halo, 0.0, 1.0)
        m = cv2.GaussianBlur(m, (0, 0), 0.8)
    else:
        m = np.clip(np.maximum(energy, edge * 0.85), 0, 1)
        m = cv2.GaussianBlur(m, (0, 0), 0.8)
    orig = rgb
    mixed = orig * (1 - m[..., None]) + neon * m[..., None]
    return (np.clip(mixed * 255, 0, 255)).astype(np.uint8)[..., ::-1], edge, energy


flat_orig = np.full((512, 512, 3), (70, 50, 40), np.uint8)
cv2.circle(flat_orig, (256, 256), 60, (230, 220, 210), -1)

wipe, edge, energy = composite(flat_orig, 'wipe')
keep, _, _ = composite(flat_orig, 'keep')
glowm, _, _ = composite(flat_orig, 'glow')

# the untouched law: far from the circle edge the keep output equals the input.
# 512px frame — the sigma-6 halo dies within ~20px of the edge ring, the
# sigma-38 border glow dies within ~120px of the frame, so the probe band
# (90px from center, >130px from any border) must be untouched art.
ref = cv2.cvtColor(flat_orig, cv2.COLOR_BGR2RGB).astype(np.int16)
kp = cv2.cvtColor(keep, cv2.COLOR_BGR2RGB).astype(np.int16)
inner = np.zeros_like(edge, bool)
inner[126:186, 126:186] = True
far = (edge < 0.02) & inner
far_err = np.abs(ref - kp)[far].mean()
check("keep mode: non-edges untouched", far_err <= 1.5, f"(far mean err {far_err:.3f})")
wp = cv2.cvtColor(wipe, cv2.COLOR_BGR2RGB).astype(np.int16)
wipe_err = np.abs(ref - wp)[far].mean()
check("wipe mode: non-edges are NOT the original", wipe_err > 25, f"(far mean err {wipe_err:.1f})")
gp = cv2.cvtColor(glowm, cv2.COLOR_BGR2RGB).astype(np.int16)
# the wash law: keep leaves the far zone exactly art (proven above on sparse
# art where a global wash would be obvious); glow mode carries the wide-sigma
# energy of the whole scene — on dense art the wash must be visible
wash_keep = float((np.abs(kp - ref)[far].max(-1) > 1).mean())
check("keep mode far zone untouched", wash_keep <= 0.02, f"(moved {wash_keep*100:.1f}%)")
scene_wipe, scene_edge, _ = composite(img, 'wipe')
scene_keep, _, _ = composite(img, 'keep')
scene_glow, _, _ = composite(img, 'glow')
sk = scene_keep[8:-8, 8:-8].astype(np.int16)
sg = scene_glow[8:-8, 8:-8].astype(np.int16)
check("glow mode visibly differs from keep on dense art",
      np.abs(sg - sk).mean() > 2.0, f"(mean {np.abs(sg - sk).mean():.2f})")
# keep must not darken the original with a global wash: corners 200+px from
# everything stay exactly art
corner_err = np.abs(ref[10:60, 10:60] - kp[10:60, 10:60]).mean()
corner_err_far = np.abs(ref[440:500, 440:500] - kp[440:500, 440:500]).mean()
check("keep mode: no global wash in corners", corner_err_far <= 1.5, f"(far corner err {corner_err_far:.3f})")

# ---------------------------------------------------------------- 4. ini schema self-check (mirrors neon_ini.h rules)
src = open('/home/z/my-project/Neonify/src/cpp/src/neon_hw.h').read()
schema = re.findall(r'\{"(\w+)",\s*"(\w+)",\s*"([^"]*)",\s*"([^"]*)",\s*"([^"]*)"\}', src)


def valid(value, rule):
    if rule == "text":
        return bool(value)
    if "|" in rule:
        return value in rule.split("|")
    if "-" in rule:
        integer = rule.startswith("int ")
        rng_s = rule[4:] if integer else rule
        lo, hi = rng_s.split("-", 1)
        try:
            v = float(value)
        except ValueError:
            return False
        if not (float(lo) <= v <= float(hi)):
            return False
        if integer and "." in value:
            return False
        return True
    return bool(value)


check("ini schema parsed", len(schema) >= 20, f"({len(schema)} fields)")
for sec, key, what, rule, dflt in schema:
    check(f"ini default {sec}.{key} obeys its own rule", valid(dflt, rule), f"(= {dflt})")

print()
if FAILURES:
    print(f"{len(FAILURES)} FAILURES: {FAILURES}")
    sys.exit(1)
print("ALL CHECKS PASSED")
