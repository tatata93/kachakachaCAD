"""Run the visible Qt practical scenario and retain raw UTF-8 diagnostics."""
import argparse, os, subprocess, json, hashlib
from datetime import datetime, timezone
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--fixture',required=True);p.add_argument('--output',required=True)
p.add_argument('--exe',default='build-msvc2022-x64/Release/kachakacha_cad_next.exe')
a=p.parse_args();out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
if (out/'execution.log').exists():raise SystemExit('Use a new output directory to preserve the previous run.')
digest=lambda path:hashlib.sha256(Path(path).read_bytes()).hexdigest()
now=lambda:datetime.now(timezone.utc).isoformat()
env={k.upper():v for k,v in os.environ.items()}
env.update(QT_QPA_PLATFORM='windows',KACHACAD_SELF_TEST_FILTER='HP-PRACTICAL',
           KACHACAD_PRACTICAL_FILE=str(Path(a.fixture).resolve()),KACHACAD_PRACTICAL_OUTPUT=str(out))
manifest=dict(started_utc=now(),status='running',fixture=str(Path(a.fixture).resolve()),
    fixture_sha256=digest(a.fixture),executable=str(Path(a.exe).resolve()),executable_sha256=digest(a.exe),
    settings={k:v for k,v in env.items() if k.startswith('KACHACAD_PRACTICAL_') or k=='QT_QPA_PLATFORM'})
with (out/'execution.log').open('wb') as log:
    child=subprocess.Popen([str(Path(a.exe).resolve()),'--self-test'],env=env,stdout=log,stderr=subprocess.STDOUT)
    manifest['pid']=child.pid;(out/'run-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    code=child.wait()
manifest.update(finished_utc=now(),status='passed' if code==0 else 'failed',exit_code=code,
    fixture_unchanged=digest(a.fixture)==manifest['fixture_sha256'])
(out/'run-manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print(f'Exit {code}; log: {out / "execution.log"}')
raise SystemExit(code)
