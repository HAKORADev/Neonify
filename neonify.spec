# -*- mode: python ; coding: utf-8 -*-
import os
import sys

a = Analysis(
    ['src/python/neonify.py'],
    pathex=['src', 'src/python'],
    binaries=[],
    datas=[('src/assets/logo.png', '.')],
    hiddenimports=['gui'],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=['packaging/runtime_hook.py'],
    excludes=['torch', 'torchvision', 'torchaudio', 'tkinter', 'matplotlib', 'numpy.f2py'],
    noarchive=False,
)

a.binaries = [b for b in a.binaries if not os.path.basename(b[0]).lower().startswith('api-ms-')]

pyz = PYZ(a.pure)

exe_kwargs = {}
if sys.platform == 'win32':
    exe_kwargs['icon'] = 'packaging/windows/logo.ico'

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,
    name='neonify',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    **exe_kwargs,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name='neonify',
)
