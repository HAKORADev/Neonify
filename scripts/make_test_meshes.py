#!/usr/bin/env python3
"""generates real test geometry: a uv sphere, a torus, a beveled box and a
twisted prism — different densities, different silhouettes — so the 3d path
gets exercised on shapes that did not come from a test fixture."""
import math
import os
import sys

out_dir = sys.argv[1] if len(sys.argv) > 1 else "build"
os.makedirs(out_dir, exist_ok=True)


def write_obj(name, verts, faces):
    path = os.path.join(out_dir, name)
    with open(path, "w") as f:
        for v in verts:
            f.write(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")
        for face in faces:
            f.write("f " + " ".join(str(i + 1) for i in face) + "\n")
    print(f"{path}: {len(verts)} verts, {len(faces)} faces")


def uv_sphere(rings=24, segs=32, r=1.0):
    verts, faces = [], []
    for i in range(rings + 1):
        phi = math.pi * i / rings
        for j in range(segs):
            theta = 2 * math.pi * j / segs
            verts.append((r * math.sin(phi) * math.cos(theta),
                          r * math.cos(phi),
                          r * math.sin(phi) * math.sin(theta)))
    for i in range(rings):
        for j in range(segs):
            a = i * segs + j
            b = i * segs + (j + 1) % segs
            c = (i + 1) * segs + (j + 1) % segs
            d = (i + 1) * segs + j
            faces.append((a, b, c))
            faces.append((a, c, d))
    return verts, faces


def torus(major=32, minor=16, R=1.0, r=0.35):
    verts, faces = [], []
    for i in range(major):
        u = 2 * math.pi * i / major
        for j in range(minor):
            v = 2 * math.pi * j / minor
            verts.append(((R + r * math.cos(v)) * math.cos(u),
                          r * math.sin(v),
                          (R + r * math.cos(v)) * math.sin(u)))
    for i in range(major):
        for j in range(minor):
            a = i * minor + j
            b = i * minor + (j + 1) % minor
            c = ((i + 1) % major) * minor + (j + 1) % minor
            d = ((i + 1) % major) * minor + j
            faces.append((a, b, c))
            faces.append((a, c, d))
    return verts, faces


def beveled_box(sub=6, s=1.0, bevel=0.12):
    # a rounded cube: superellipse cross-section swept along the third axis
    verts, faces = [], []
    n = 2 * sub
    ring = []
    for j in range(n):
        t = 2 * math.pi * j / n
        x = s * math.copysign(abs(math.cos(t)) ** (1.0 / (1.0 + bevel * 4)), math.cos(t))
        y = s * math.copysign(abs(math.sin(t)) ** (1.0 / (1.0 + bevel * 4)), math.sin(t))
        ring.append((x, y))
    layers = 10
    for i in range(layers):
        z = -s + 2 * s * i / (layers - 1)
        for x, y in ring:
            verts.append((x, y, z))
    for i in range(layers - 1):
        for j in range(n):
            a = i * n + j
            b = i * n + (j + 1) % n
            c = (i + 1) * n + (j + 1) % n
            d = (i + 1) * n + j
            faces.append((a, b, c))
            faces.append((a, c, d))
    return verts, faces


def twisted_prism(seg=48, sides=6, twists=1.5):
    verts, faces = [], []
    layers = seg
    for i in range(layers):
        t = i / (layers - 1)
        z = -1.0 + 2.0 * t
        ang0 = twists * math.pi * t
        for k in range(sides):
            a = ang0 + 2 * math.pi * k / sides
            rad = 0.55 + 0.45 * math.sin(math.pi * t)
            verts.append((rad * math.cos(a), rad * math.sin(a), z))
    for i in range(layers - 1):
        for k in range(sides):
            a = i * sides + k
            b = i * sides + (k + 1) % sides
            c = (i + 1) * sides + (k + 1) % sides
            d = (i + 1) * sides + k
            faces.append((a, b, c))
            faces.append((a, c, d))
    return verts, faces


write_obj("sphere.obj", *uv_sphere())
write_obj("torus.obj", *torus())
write_obj("bevelbox.obj", *beveled_box())
write_obj("twist.obj", *twisted_prism())
