#!/usr/bin/env python3
"""generate neon_plugins.h (static qt plugin imports) from the qt source tree.
usage: gen_plugins.py <out_header> <qtbase_src> <qtmultimedia_src> <windows|linux>"""
import os
import re
import sys


def find_class(folder):
    for root, _, files in os.walk(folder):
        for f in files:
            if not f.endswith(('.h', '.cpp')):
                continue
            p = os.path.join(root, f)
            try:
                src = open(p, encoding='utf-8', errors='ignore').read()
            except OSError:
                continue
            i = src.find('Q_PLUGIN_METADATA')
            if i < 0:
                continue
            seg = src[max(0, i - 500):i]
            names = re.findall(r'class\s+([A-Za-z0-9_]+)', seg)
            if names:
                return names[-1]
    return None


def main():
    out, qtbase, qtmulti, platform = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
    if platform == 'windows':
        targets = [
            (os.path.join(qtbase, 'src', 'plugins', 'platforms', 'windows'), 'qwindows'),
            (os.path.join(qtbase, 'src', 'plugins', 'audio', 'qtaudio_windows'), 'qtaudio_windows'),
            (os.path.join(qtbase, 'src', 'plugins', 'imageformats', 'jpeg'), 'qjpeg'),
            (os.path.join(qtmulti, 'src', 'plugins', 'wmf'), 'wmfservice'),
            (os.path.join(qtmulti, 'src', 'plugins', 'dsengine'), 'dsengine'),
        ]
    else:
        targets = [
            (os.path.join(qtbase, 'src', 'plugins', 'platforms', 'xcb'), 'qxcb'),
            (os.path.join(qtbase, 'src', 'plugins', 'audio', 'qtaudio_alsa'), 'qtaudio_alsa'),
            (os.path.join(qtbase, 'src', 'plugins', 'imageformats', 'jpeg'), 'qjpeg'),
        ]
    pairs = []
    for folder, need in targets:
        cls = find_class(folder)
        print('plugin', need, '->', cls)
        if cls:
            pairs.append(cls)
    os.makedirs(os.path.dirname(out) or '.', exist_ok=True)
    with open(out, 'w') as f:
        f.write('#pragma once\n#include <QtPlugin>\n')
        for cls in pairs:
            f.write('Q_IMPORT_PLUGIN(%s)\n' % cls)
    print(open(out).read())


if __name__ == '__main__':
    main()
