#!/usr/bin/env python3
"""neonify v2 test suite — run from repo root: python3 scripts/test_neonify_v2.py"""
import io
import os
import re
import shutil
import subprocess
import sys
import tempfile
from contextlib import redirect_stdout

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'src', 'python'))

import numpy as np  # noqa: E402

import neonify as N  # noqa: E402

ASSETS = os.path.join(ROOT, 'test_assets')
WORK = tempfile.mkdtemp(prefix='neonify_test_')
os.chdir(WORK)

PASS, FAIL = 0, 0


def check(name, ok, detail=''):
    global PASS, FAIL
    if ok:
        PASS += 1
        print(f"  PASS  {name}")
    else:
        FAIL += 1
        print(f"  FAIL  {name}  {detail}")


def ffmpeg_ok():
    try:
        subprocess.run(['ffmpeg', '-version'], capture_output=True, timeout=20)
        return True
    except Exception:
        return False


HAS_FF = ffmpeg_ok()

# ---- tracker --------------------------------------------------------------
print("[tracker]")
t = N.StageTracker()
t.set_stages(['a', 'b'])
t.begin_stage(0)
t.step(0.5)
check('tracker holds stage at 50%', abs(t.pct - 50.0) < 1e-6, str(t.pct))
t.complete_stage(0)
t.begin_stage(1)
t.step(0.3)
check('tracker new stage restarts %', abs(t.pct - 30.0) < 1e-6, str(t.pct))
t.complete_stage(1)
t.finish()
check('tracker full run survives', True)

# ---- naming law -----------------------------------------------------------
print("[naming]")
ts = N.timestamp_suffix()
check('timestamp format _YYMMDDHHMMSS', re.fullmatch(r'_\d{12}', ts) is not None, ts)
d = N._out_default('photo.png', 'ice')
check('effect tag in default name', '_ice_' in d and d.endswith('.png'), d)
rd = N._results_path(d)
check('results/ folder in default path', 'results' in rd.split(os.sep), rd)
check('results dir auto-created', os.path.isdir('results'))

# ---- banner ----------------------------------------------------------------
print("[banner]")
buf = io.StringIO()
with redirect_stdout(buf):
    N.print_banner()
banner = buf.getvalue()
check('banner prints NEONIFY', 'NEONIFY' in banner.replace(' ', '').replace('\n', ''), banner[:60])

# ---- image + text preservation ----------------------------------------------
print("[image]")
shutil.copy(os.path.join(ASSETS, 'text.png'), 'text.png')
out_img = N._results_path(N._out_default('text.png', 'electric'))
out_path, aux = N.neonize_image_file('text.png', out_img)
check('image output exists', os.path.isfile(out_path), out_path)
check('noise estimate present', 'noise' in aux)

import cv2  # noqa: E402
src = cv2.imread('text.png', cv2.IMREAD_GRAYSCALE)
res = cv2.imread(out_path, cv2.IMREAD_GRAYSCALE)
sep = float(np.corrcoef(src.ravel(), res.ravel())[0, 1])
check('text structure survives (corr > 0.5)', sep > 0.5, f'{sep:.2f}')

# ---- alpha preservation -------------------------------------------------------
print("[alpha]")
img = np.zeros((100, 100, 4), np.uint8)
cv2.circle(img, (50, 50), 30, (255, 255, 255, 255), -1)
cv2.imwrite('alpha_in.png', img)
a_out = N._results_path(N._out_default('alpha_in.png', 'ice'))
N.neonize_image_file('alpha_in.png', a_out)
r = cv2.imread(a_out, cv2.IMREAD_UNCHANGED)
ok4 = r is not None and r.ndim == 3 and r.shape[2] == 4
check('transparent input -> BGRA output', ok4)
if ok4:
    check('far corners stay transparent', int(r[2, 2, 3]) == 0, str(int(r[2, 2, 3])))
    rim = int(r[20, 50, 3])  # top of the circle rim
    check('tube rim carries alpha', rim > 40, str(rim))
else:
    check('far corners stay transparent', False)
    check('tube rim carries alpha', False)

# ---- noisy b&w video ----------------------------------------------------------
print("[video]")
if HAS_FF:
    shutil.copy(os.path.join(ASSETS, 'noisy_bw.mp4'), 'noisy_bw.mp4')
    cap = cv2.VideoCapture('noisy_bw.mp4')
    ok, frame0 = cap.read()
    cap.release()
    v_out = N._results_path(N._out_default('noisy_bw.mp4', 'crimson', '.mp4'))
    vpath, nframes = N.process_video_file('noisy_bw.mp4', v_out, 'crimson', 1.0, 0.12,
                                          tracker=N.StageTracker())
    check('video output exists', os.path.isfile(vpath), vpath)
    check('video frames counted', nframes > 5, str(nframes))
    cap2 = cv2.VideoCapture(vpath)
    ok2, of = cap2.read()
    cap2.release()
    if ok2 and ok:
        g0 = cv2.cvtColor(frame0, cv2.COLOR_BGR2GRAY).ravel().astype(np.float32)
        go = cv2.cvtColor(of, cv2.COLOR_BGR2GRAY).ravel().astype(np.float32)
        m = min(len(g0), len(go))
        sepv = float(np.corrcoef(g0[:m], go[:m])[0, 1])
        check('noisy video: not a raw copy (corr < 0.95)', sepv < 0.95, f'{sepv:.3f}')
    else:
        check('noisy video: not a raw copy', False, 'decode failed')
    check('video name carries palette', '_crimson_' in vpath, vpath)
else:
    check('video flow (ffmpeg present)', False, 'no ffmpeg in sandbox')

# ---- audio profiles ------------------------------------------------------------
print("[audio]")
if HAS_FF:
    shutil.copy(os.path.join(ASSETS, 'stereo.wav'), 'stereo.wav')
    profiles_ok = True
    last = ''
    for prof in N.AUDIO_PROFILES:
        p_out = N._results_path(N._out_default('stereo.wav', prof, '.wav'))
        try:
            N.neonize_audio_file('stereo.wav', p_out, prof, 1.0)
            okf = os.path.isfile(p_out) and os.path.getsize(p_out) > 1000
        except Exception as e:
            okf = False
            print('   ', prof, 'error:', e)
        profiles_ok = profiles_ok and okf
        last = p_out
    check('all 7 audio profiles render', profiles_ok, str(N.AUDIO_PROFILES))
    check('audio name carries profile', f"_{N.AUDIO_PROFILES[-1]}_" in last, last)
    check('advanced schema exists', isinstance(N.AUDIO_ADVANCED_SCHEMA, dict)
          and len(N.AUDIO_ADVANCED_SCHEMA) > 3)
    a_out2 = N._results_path(N._out_default('stereo.wav', 'ghost', '.wav'))
    N.neonize_audio_file('stereo.wav', a_out2, 'ghost', 1.0,
                         advanced={k: {} for k in N.AUDIO_ADVANCED_SCHEMA})
    check('advanced overrides accepted', os.path.isfile(a_out2))
else:
    check('audio flow (ffmpeg present)', False, 'no ffmpeg')

# ---- spatial -------------------------------------------------------------------
print("[spatial]")
rng = np.random.RandomState(0)
em = N.spatial_energy_map(rng.randn(22050, ).astype(np.float32),
                          rng.randn(22050, ).astype(np.float32), 22050, 24, 24)
check('spatial map shape (n,2) in -1..1',
      em is not None and em.shape == (24, 2) and float(np.abs(em).max()) <= 1.0)

# ---- 3d ---------------------------------------------------------------------
print("[3d]")
shutil.copy(os.path.join(ASSETS, 'cube.obj'), 'cube.obj')
verts, faces = N.load_mesh('cube.obj')
check('cube loads', len(verts) >= 8 and len(faces) >= 12, f'{len(verts)}v {len(faces)}f')
m_out = N._results_path(N._out_default('cube.obj', 'violet3d', '.png'))
N.neonize_mesh_file('cube.obj', m_out, 'violet', 1.0, 0, 30.0, 20.0)
check('mesh render exists', os.path.isfile(m_out))
r_out = N._results_path(N._out_default('text.png', 'golden3d', '.png'))
N.neonize_relief_file('text.png', r_out, 'golden', 1.0, 0.85, 0, 30.0, 35.0, export_mesh=True)
check('relief render exists', os.path.isfile(r_out))
check('relief exports mesh obj', os.path.isfile(r_out.replace('.png', '_mesh.obj')))

# ---- cli surface -----------------------------------------------------------------
print("[cli]")
from neonify import build_parser  # noqa: E402
p = build_parser()
ns = p.parse_args(['image', 'x.png', '--palette', 'ice', '--neon-audio', '--profile', 'ghost'])
check('argparse palette choices', ns.palette == 'ice')
src_txt = open(os.path.join(ROOT, 'src', 'python', 'neonify.py')).read()
check('no --device flag anywhere', "'--device'" not in src_txt and '"--device"' not in src_txt)
check('cli subcommand enters interactive mode', "args.command == 'cli'" in src_txt)
check('no cuda/gpu theater in engine', 'cuda' not in src_txt.lower())

print()
print(f"=== {PASS} passed, {FAIL} failed ===")
sys.exit(1 if FAIL else 0)
