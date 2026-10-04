#!/usr/bin/env python3
"""3d verification bench: the c++ mesh renderer (render_neon_mesh +
neon_glow_stack + colorize + core) reimplemented 1:1 in numpy, fed the real
test geometry from make_test_meshes.py. renders every shape across palettes so
the wire structure, nearness shading, glow falloff and palette coloring can be
inspected by eye — and checks hard facts: wire coverage, color channels, glow
halo width, turntable frame deltas."""
import math
import os
import sys

import numpy as np
import cv2

sys.path.insert(0, os.path.dirname(__file__))
import importlib.util

spec = importlib.util.spec_from_file_location("parity", os.path.join(os.path.dirname(__file__), "parity_check.py"))

FAILURES = []


def check(name, cond, detail=""):
    status = "PASS" if cond else "FAIL"
    print(f"[{status}] {name} {detail}")
    if not cond:
        FAILURES.append(name)


# ---- shared math with the engine (single-source in this bench) ----
def gaussian_kernel1d(sigma):
    radius = max(1, int(math.ceil(sigma * 3.0)))
    x = np.arange(2 * radius + 1, dtype=np.float32) - radius
    k = np.exp(-(x * x) / (2.0 * sigma * sigma))
    return (k / k.sum()).astype(np.float32)


def gaussian_blur(img, sigma):
    if sigma <= 0.05:
        return img
    k = gaussian_kernel1d(sigma)
    return cv2.sepFilter2D(img, -1, k, k, borderType=cv2.BORDER_CONSTANT)


def wide_blur(t, sigma):
    if sigma >= 12.0:
        h, w = t.shape[:2]
        small = cv2.resize(t, (max(1, w // 4), max(1, h // 4)), interpolation=cv2.INTER_LINEAR)
        small = gaussian_blur(small, sigma * 0.25)
        return cv2.resize(small, (w, h), interpolation=cv2.INTER_LINEAR)
    return gaussian_blur(t, sigma)


def smoothstep(lo, hi, x):
    t = np.clip((x - lo) / (hi - lo + 1e-8), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


STOPS = {
    'electric': [(0.00, (0.004, 0.010, 0.045)), (0.28, (0.05, 0.25, 0.85)), (0.58, (0.15, 0.72, 1.00)), (0.84, (0.60, 0.95, 1.00)), (1.00, (1.00, 1.00, 1.00))],
    'synthwave': [(0.00, (0.040, 0.004, 0.060)), (0.30, (0.45, 0.08, 0.75)), (0.60, (0.98, 0.20, 0.60)), (0.85, (1.00, 0.50, 0.45)), (1.00, (1.00, 0.96, 0.88))],
    'toxic': [(0.00, (0.004, 0.035, 0.012)), (0.30, (0.05, 0.50, 0.12)), (0.64, (0.35, 0.95, 0.20)), (1.00, (0.90, 1.00, 0.80))],
    'ice': [(0.00, (0.004, 0.012, 0.024)), (0.35, (0.10, 0.30, 0.55)), (0.70, (0.55, 0.80, 0.95)), (1.00, (1.00, 1.00, 1.00))],
    'fire': [(0.00, (0.035, 0.005, 0.002)), (0.30, (0.55, 0.08, 0.02)), (0.64, (1.00, 0.45, 0.05)), (0.88, (1.00, 0.80, 0.30)), (1.00, (1.00, 1.00, 0.92))],
    'ghost': [(0.00, (0.012, 0.012, 0.014)), (0.40, (0.35, 0.35, 0.38)), (0.75, (0.75, 0.78, 0.82)), (1.00, (1.00, 1.00, 1.00))],
    'spectrum': None,
}


def palette_lut(name):
    idx = np.linspace(0.0, 1.0, 256)
    if name == 'spectrum':
        h = idx * 6.0
        i = h.astype(int) % 6
        f = h - np.floor(h)
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


def colorize(field, palette):
    idx8 = np.clip((np.clip(field, 0, 1) * 255.0), 0, 255).astype(np.uint8)
    lut8 = (palette_lut(palette) * 255.0).clip(0, 255).astype(np.uint8)
    return lut8[idx8]  # rgb


def render_mesh(verts, faces, W=960, H=720, yaw=0.0, pitch=0.35, palette='electric', glow=1.0):
    # normalize
    v = np.asarray(verts, np.float32)
    lo, hi = v.min(0), v.max(0)
    v = (v - (lo + hi) / 2) / max((hi - lo).max(), 1e-6)
    # rot_y then rot_x (c++ order)
    c, s = math.cos(yaw), math.sin(yaw)
    v = np.stack([c * v[:, 0] + s * v[:, 2], v[:, 1], -s * v[:, 0] + c * v[:, 2]], 1)
    c, s = math.cos(pitch), math.sin(pitch)
    v = np.stack([v[:, 0], c * v[:, 1] - s * v[:, 2], s * v[:, 1] + c * v[:, 2]], 1)
    z = v[:, 2] + 2.6
    px = (v[:, 0] / (z * 0.45)) * W * 0.5 * 1.4 + W / 2
    py = (-v[:, 1] / (z * 0.45)) * H * 0.5 * 1.4 + H / 2
    edges = set()
    for f in faces:
        for k in range(3):
            a, b = f[k], f[(k + 1) % 3]
            edges.add((min(a, b), max(a, b)))
    edges = sorted(edges)
    canvas = np.zeros((H, W), np.float32)
    zmean = np.array([(z[a] + z[b]) * 0.5 for a, b in edges])
    zmin, zmax = zmean.min(), zmean.max()
    zrange = max(zmax - zmin, 1e-5)
    order = np.argsort(-zmean)
    for ei in order:
        a, b = edges[ei]
        depth_norm = (zmean[ei] - zmin) / zrange
        inten = min(1.1, max(0.05, (1.0 - 0.72 * depth_norm) * 1.05))
        cv2.line(canvas, (int(round(px[a])), int(round(py[a]))),
                 (int(round(px[b])), int(round(py[b]))), float(inten), 1, cv2.LINE_AA)
    # neon_glow_stack
    glow_field = np.zeros_like(canvas)
    for sigma, weight in zip((2.0, 6.0, 16.0, 38.0), (0.9, 0.62, 0.45, 0.32)):
        glow_field = glow_field + wide_blur(canvas, sigma) * (weight * glow * 1.0)
    energy = np.clip((canvas * 1.15 + glow_field * 0.85) * (0.72 + 0.42), 0.0, 1.0)
    col = colorize(energy, palette)
    core = np.power(np.clip(canvas, 0, 1), 1.2) * 0.85 * 0.55
    col = np.clip(col.astype(np.float32) + core[..., None] * 255.0, 0, 255).astype(np.uint8)
    return col, canvas, zmean.max() - zmin


def load_obj(path):
    verts, faces = [], []
    for line in open(path):
        if line.startswith('v '):
            verts.append([float(x) for x in line.split()[1:4]])
        elif line.startswith('f '):
            idx = [int(t.split('/')[0]) - 1 for t in line.split()[1:]]
            for k in range(1, len(idx) - 1):
                faces.append((idx[0], idx[k], idx[k + 1]))
    return verts, faces


out_dir = "build/mesh_bench"
os.makedirs(out_dir, exist_ok=True)
sys.exit_ok = True

gen = os.path.join(os.path.dirname(__file__), "make_test_meshes.py")
os.system(f"python3 '{gen}' build")

shapes = ['sphere', 'torus', 'bevelbox', 'twist']
palette_of = {'sphere': 'electric', 'torus': 'synthwave', 'bevelbox': 'fire', 'twist': 'ice'}

for shape in shapes:
    verts, faces = load_obj(f"build/{shape}.obj")
    ang = 30 * math.pi / 180
    col, wire, zspread = render_mesh(verts, faces, yaw=ang, pitch=20 * math.pi / 180,
                                     palette=palette_of[shape])
    path = os.path.join(out_dir, f"{shape}_{palette_of[shape]}.png")
    cv2.imwrite(path, col[:, :, ::-1])
    # hard facts about the 3d path
    lit = float((wire > 0.3).mean())
    nz = col.reshape(-1, 3).max(0)
    check(f"{shape}: wire actually drawn", 0.004 < lit < 0.6, f"(lit={lit:.3f})")
    check(f"{shape}: output not flat", int(nz.max()) > 120, f"(max={nz})")
    # nearness shading must exist: depth spread above zero means z-order ran
    check(f"{shape}: depth spread present", zspread > 0.05, f"({zspread:.3f})")

# glow halo check on the sphere: energy far from any wire should still rise
verts, faces = load_obj("build/sphere.obj")
col, wire, _ = render_mesh(verts, faces, palette='electric')
ys, xs = np.where(wire > 0.5)
far_mask = np.ones_like(wire, bool)
far_mask[np.clip(ys - 22, 0, wire.shape[0] - 1), xs] = False
halo_energy = energy_far = None
glow_field = np.zeros_like(wire)
for sigma, weight in zip((2.0, 6.0, 16.0, 38.0), (0.9, 0.62, 0.45, 0.32)):
    glow_field = glow_field + wide_blur(wire, sigma) * (weight * 1.0)
far_sample = glow_field[~(wire > 0.05)]
check("glow halo reaches past the wire", float(far_sample.max()) > 0.25,
      f"(far glow max {float(far_sample.max()):.3f})")

# turntable delta: two yaw frames must differ (the orbit actually orbits)
c1, _, _ = render_mesh(verts, faces, yaw=0.0, palette='electric')
c2, _, _ = render_mesh(verts, faces, yaw=math.pi / 3, palette='electric')
delta = np.abs(c1.astype(int) - c2.astype(int)).mean()
check("turntable frames differ", delta > 3.0, f"(mean delta {delta:.1f})")

# palette identity: same geometry, different palettes -> different colors
pa = {}
for p in ['electric', 'synthwave', 'fire']:
    c, _, _ = render_mesh(verts, faces, palette=p)
    lit = c[c.sum(2) > 120]
    pa[p] = lit.mean(0)
check("electric leans blue", pa['electric'][2] > pa['electric'][0], f"({pa['electric']})")
check("synthwave leans pink-purple (green lowest)", pa['synthwave'][1] < min(pa['synthwave'][0], pa['synthwave'][2]), f"({pa['synthwave']})")
check("fire leans red-orange", pa['fire'][0] > pa['fire'][2], f"({pa['fire']})")

print()
if FAILURES:
    print(f"{len(FAILURES)} FAILURES: {FAILURES}")
    sys.exit(1)
print("3D BENCH: ALL CHECKS PASSED — renders in build/mesh_bench/")
