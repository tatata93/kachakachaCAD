"""Run the visible Qt practical scenario and retain raw UTF-8 diagnostics."""
import argparse, os, subprocess
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--fixture',required=True);p.add_argument('--output',required=True)
p.add_argument('--exe',default='build-msvc2022-x64/Release/kachakacha_cad_next.exe')
a=p.parse_args();out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
env={k.upper():v for k,v in os.environ.items()}
env.update(QT_QPA_PLATFORM='windows',KACHACAD_SELF_TEST_FILTER='HP-PRACTICAL',
           KACHACAD_PRACTICAL_FILE=str(Path(a.fixture).resolve()),KACHACAD_PRACTICAL_OUTPUT=str(out))
with (out/'execution.log').open('wb') as log:
    result=subprocess.run([str(Path(a.exe).resolve()),'--self-test'],env=env,stdout=log,stderr=subprocess.STDOUT)
print(f'Exit {result.returncode}; log: {out / "execution.log"}')
raise SystemExit(result.returncode)
