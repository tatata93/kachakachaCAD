#!/usr/bin/env python3
"""Check an ordinary Windows GUI launch, not an offscreen self-test."""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess


def subsystem(path):
    data = path.read_bytes()
    offset = struct.unpack_from('<I', data, 60)[0]
    if data[offset:offset + 4] != b'PE\0\0':
        raise ValueError('Not a Windows executable')
    return struct.unpack_from('<H', data, offset + 24 + 68)[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if os.name != 'nt':
        raise SystemExit('Desktop startup check requires Windows')
    exe = args.exe.resolve()
    if subsystem(exe) != 2:
        raise SystemExit('Release must use the Windows GUI subsystem, not console')
    args.output.mkdir(parents=True, exist_ok=True)
    for inherited in ('windows', 'offscreen'):
        report = (args.output / (inherited + '.json')).resolve()
        env = dict(os.environ)
        env.update(QT_QPA_PLATFORM=inherited, KACHACAD_STARTUP_REPORT=str(report), KACHACAD_STARTUP_EXIT='1')
        with (args.output / (inherited + '.log')).open('w', encoding='utf-8') as log:
            # No command-line test switches: follow the real main/event-loop path.
            result = subprocess.run([str(exe)], cwd=exe.parent, env=env, stdin=subprocess.DEVNULL,
                                    stdout=log, stderr=subprocess.STDOUT, timeout=30)
        if result.returncode or not report.is_file():
            raise SystemExit('Normal desktop launch failed: ' + inherited)
        proof = json.loads(report.read_text(encoding='utf-8'))
        if not proof.get('ready') or not proof.get('event_loop') or proof.get('platform') != 'windows':
            raise SystemExit('No visible, exposed, rendered Windows CAD window: ' + inherited)
        print('PASS normal desktop launch, inherited=' + inherited, flush=True)


if __name__ == '__main__':
    main()
