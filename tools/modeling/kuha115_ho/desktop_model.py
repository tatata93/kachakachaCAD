"""Observe a model in the released CAD's ordinary Windows event loop."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, time


def main():
    ap=argparse.ArgumentParser();ap.add_argument('model',type=Path);ap.add_argument('exe',type=Path)
    ap.add_argument('--work',required=True,type=Path);a=ap.parse_args()
    model,exe,work=a.model.resolve(),a.exe.resolve(),a.work.resolve()
    if os.name!='nt':raise SystemExit('This on-screen check requires Windows')
    work.mkdir(parents=True,exist_ok=False);report=work/'desktop.json'
    env={k:v for k,v in os.environ.items() if not k.upper().startswith('KACHACAD_')}
    env.update(QT_QPA_PLATFORM='windows',KACHACAD_STARTUP_REPORT=str(report),KACHACAD_STARTUP_EXIT='1',
               KACHACAD_DOCUMENT_CACHE_DIR=str(work/'cache'),TEMP=str(work),TMP=str(work))
    start=time.monotonic()
    with (work/'desktop.log').open('w',encoding='utf-8') as log:
        process=subprocess.run([str(exe),'--open',str(model),'--size','1600x1000'],cwd=work,env=env,
            stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,timeout=600)
    proof=json.loads(report.read_text(encoding='utf-8'))
    assert process.returncode==0 and proof['ready'] and proof['event_loop'] and proof['platform']=='windows'
    assert proof['exposed'] and proof['on_screen'] and proof['rendered'] and (work/'desktop.json.png').is_file()
    proof.update(seconds=time.monotonic()-start,sourceSha256=hashlib.sha256(model.read_bytes()).hexdigest(),
                 exeSha256=hashlib.sha256(exe.read_bytes()).hexdigest(),exitCode=process.returncode)
    report.write_text(json.dumps(proof,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    print(json.dumps(proof,ensure_ascii=False))


if __name__=='__main__':main()
