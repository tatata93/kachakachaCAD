"""Regenerate native features using a specified released CAD, with isolated evidence."""
from pathlib import Path
import argparse, os, json, subprocess, time, hashlib, uuid


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('model',type=Path)
    parser.add_argument('exe',type=Path)
    parser.add_argument('--work',type=Path,required=True)
    args=parser.parse_args();model=args.model.resolve();exe=args.exe.resolve();work=args.work.resolve()
    work.mkdir(parents=True,exist_ok=False)
    token=str(uuid.uuid4())
    (work/'request.json').write_text(json.dumps(dict(version=1,token=token,source=str(model))),encoding='utf-8')
    env={k:v for k,v in os.environ.items() if not k.upper().startswith('KACHACAD_')}
    env.update(QT_QPA_PLATFORM='offscreen',KACHACAD_DOCUMENT_CACHE_DIR=str(work/'cache'),TEMP=str(work),TMP=str(work))
    before=hashlib.sha256(model.read_bytes()).hexdigest();start=time.monotonic()
    with (work/'worker.log').open('w',encoding='utf-8') as log:
        process=subprocess.run([str(exe),'--document-job',str(work)],env=env,cwd=work,stdout=log,
                               stderr=subprocess.STDOUT,timeout=600)
    response=json.loads((work/'response.json').read_text(encoding='utf-8')) if (work/'response.json').exists() else {}
    response.update(exit=process.returncode,seconds=time.monotonic()-start,
                    exeSha=hashlib.sha256(exe.read_bytes()).hexdigest(),modelSha=before,
                    unchanged=hashlib.sha256(model.read_bytes()).hexdigest()==before)
    passed=response.get('ok') and response.get('token')==token and response.get('version')==1
    passed=bool(passed and response['unchanged'] and process.returncode==0 and (work/'prepared.bin').is_file())
    response['passed']=passed
    (work/'validation.json').write_text(json.dumps(response,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(response,ensure_ascii=False));return 0 if passed else 1


if __name__=='__main__':raise SystemExit(main())
