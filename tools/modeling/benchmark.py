"""Run the actual CAD heavy-model validation, rendering, and export path.
No CAD files are replaced; generated exports go under --output.
"""
import argparse
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(__doc__)
parser.add_argument('--exe', type=Path, default=root / 'build-msvc2022-x64/Release/kachakacha_cad_next.exe')
parser.add_argument('--model', type=Path, default=root / 'samples/series115-heavy/series115-1000-1-80.kcd2')
parser.add_argument('--output', type=Path, default=root / '_claudeout/115-heavy')
parser.add_argument('--platform', default='windows' if os.name == 'nt' else 'offscreen')
args = parser.parse_args()
args.output.mkdir(parents=True, exist_ok=True)
env = {k.upper(): v for k, v in os.environ.items()} if os.name == 'nt' else dict(os.environ)
env.update(QT_QPA_PLATFORM=args.platform, KACHACAD_SELF_TEST_FILTER='HP-HEAVY',
           KACHACAD_HEAVY_MODEL=str(args.model.resolve()), KACHACAD_HEAVY_OUTPUT=str(args.output.resolve()))
log = args.output / 'benchmark.txt'
with log.open('wb') as stream:
    result = subprocess.run([str(args.exe.resolve()), '--self-test'], cwd=root, env=env,
                            stdout=stream, stderr=subprocess.STDOUT, timeout=900)
print(log.read_text(encoding='utf-8', errors='replace'))
raise SystemExit(result.returncode)
