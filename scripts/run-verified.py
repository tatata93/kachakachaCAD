#!/usr/bin/env python3
"""Open an immutable verified release, never the executable being rebuilt."""
import argparse
import json
import subprocess
from pathlib import Path
from verification.evidence import artifact_identity

parser = argparse.ArgumentParser()
parser.add_argument('release', type=Path)
parser.add_argument('--project', type=Path)
args = parser.parse_args()
proof = json.loads((args.release / 'verification.json').read_text(encoding='utf-8'))
package = args.release / 'package'
if proof.get('status') != 'passed' or proof.get('mode') != 'full' or artifact_identity(package) != proof['artifacts']:
    raise SystemExit('Release proof is missing, failed or does not match the files')
command = [str((package / 'kachakacha_cad_next.exe').resolve())]
if args.project:
    command += ['--open', str(args.project.resolve())]
subprocess.Popen(command, cwd=package)
