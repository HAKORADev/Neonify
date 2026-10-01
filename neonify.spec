# -*- mode: python ; coding: utf-8 -*-
import os
import sys

a = Analysis(
    ['src/neonify.py'],
    pathex=['src'],
    binaries=[],
    datas=[('src/logo.png', '.')],
    hiddenimports=['gui'],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=['packaging/runtime_hook.py'],
    excludes=[],
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
    console=True,
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
