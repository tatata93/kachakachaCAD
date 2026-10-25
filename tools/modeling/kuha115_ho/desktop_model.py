"""Observe a model in the released CAD's ordinary Windows event loop."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, time


def main():
    ap=argparse.ArgumentParser();ap.add_argument('model',type=Path);ap.add_argument('exe',type=Path)
    ap.add_argument('--validated-cache',type=Path,help='Reuse only a successful worker cache for these exact model/executable bytes')
    ap.add_argument('--work',required=True,type=Path);a=ap.parse_args()
    model,exe,work=a.model.resolve(),a.exe.resolve(),a.work.resolve()
    if os.name!='nt':raise SystemExit('This on-screen check requires Windows')
    work.mkdir(parents=True,exist_ok=False);report=work/'desktop.json'
    model_sha=hashlib.sha256(model.read_bytes()).hexdigest();exe_sha=hashlib.sha256(exe.read_bytes()).hexdigest()
    cache=work/'cache';receipt=None
    if a.validated_cache:
        receipt_path=a.validated_cache.resolve()/'validation.json'
        receipt=json.loads(receipt_path.read_text(encoding='utf-8'))
        assert receipt['passed'] and receipt['unchanged'] and receipt['modelSha']==model_sha and receipt['exeSha']==exe_sha
        cache=receipt_path.parent/'cache';assert cache.is_dir()
    env={k:v for k,v in os.environ.items() if not k.upper().startswith('KACHACAD_')}
    env.update(QT_QPA_PLATFORM='windows',KACHACAD_STARTUP_REPORT=str(report),KACHACAD_STARTUP_EXIT='1',
               KACHACAD_DOCUMENT_CACHE_DIR=str(cache),KACHACAD_OPEN_TRACE_DIR=str(work/'trace'),TEMP=str(work),TMP=str(work))
    start=time.monotonic()
    with (work/'desktop.log').open('w',encoding='utf-8') as log:
        process=subprocess.run([str(exe),'--open',str(model),'--size','1600x1000'],cwd=work,env=env,
            stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,timeout=600)
    proof=json.loads(report.read_text(encoding='utf-8'))
    assert process.returncode==0 and proof['ready'] and proof['event_loop'] and proof['platform']=='windows'
    assert proof['exposed'] and proof['on_screen'] and proof['rendered'] and (work/'desktop.json.png').is_file()
    stages=[json.loads(line)['stage'] for p in (work/'trace').glob('*.jsonl') for line in p.read_text(encoding='utf-8').splitlines()]
    hit='read-cache-hit' in stages
    if receipt is not None:assert hit,'Validated cache reuse did not occur; do not report it as a warm display check'
    proof.update(seconds=time.monotonic()-start,sourceSha256=model_sha,
                 exeSha256=exe_sha,exitCode=process.returncode,validatedCacheReuse=receipt is not None,cacheHitConfirmed=hit)
    report.write_text(json.dumps(proof,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    # Console encodings such as CP932 cannot represent the title's em dash.
    print(json.dumps(proof,ensure_ascii=True))


if __name__=='__main__':main()
