"""A focused continuous insertion check; never labels this scope a full release."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,shutil,subprocess,time


def main():
    ap=argparse.ArgumentParser();ap.add_argument('design',type=Path);ap.add_argument('native',type=Path);ap.add_argument('exe',type=Path);ap.add_argument('exporter',type=Path);ap.add_argument('work',type=Path);a=ap.parse_args()
    design,native,exe,exporter,w=[getattr(a,k).resolve() for k in ['design','native','exe','exporter','work']]
    w.mkdir(parents=True,exist_ok=False);(w/'bin').mkdir();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    d=json.loads(design.read_text(encoding='utf-8'));proof=json.loads((native/'validation.json').read_text(encoding='utf-8'))
    assert proof['passed'] and proof['unchanged'] and proof['exeSha']==sha(exe) and d['insertionChecks']
    for key in ['pointChecks','assemblyChecks','attachmentChecks','splitChecks']:d[key]=[]
    path=w/'design.json';path.write_text(json.dumps(d,ensure_ascii=False),encoding='utf-8')
    frozen=w/'bin'/exporter.name;shutil.copy2(exporter,frozen)
    for dll in exporter.parent.glob('*.dll'):shutil.copy2(dll,frozen.parent/dll.name)
    source=Path(__file__).resolve().parents[1]/'series115_print/export_native.cpp';shutil.copy2(source,w/'exporter-source.cpp')
    env=os.environ.copy();env['PATH']=str(exe.parent)+os.pathsep+env.get('PATH','');start=time.monotonic();utc=datetime.datetime.now(datetime.timezone.utc).isoformat()
    command=[str(frozen),str(native/'prepared.bin'),str(path),str(w/'exports'),'--check-only']
    with (w/'run.log').open('w',encoding='utf-8') as log:r=subprocess.run(command,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=600)
    result=dict(scope='Insertion checks and visible-part validity only; no final exports',passed=False,exit=r.returncode,seconds=time.monotonic()-start,
        startUtc=utc,endUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),command=command,
        originalDesignSha256=sha(design),nativeProofSha256=sha(native/'validation.json'),exporterSha256=sha(frozen),sourceSha256=sha(w/'exporter-source.cpp'))
    report=w/'exports/print-parts.json'
    if report.exists():
        p=json.loads(report.read_text(encoding='utf-8'));expected={e['id'] for e in d['entities'] if e['kind']=='part' and e['visibility']=='visible'}
        assert expected=={q['id'] for q in p['parts']} and p['preparedToken']==proof['token']
        assert len(d['insertionChecks'])==len(p['insertionChecks']) and all(all(q[k]==v for k,v in c.items()) for c,q in zip(d['insertionChecks'],p['insertionChecks']))
        result.update(reportSha256=sha(report),passed=r.returncode==0 and not p['failures'] and all(q['complete'] and q['passed'] for q in p['insertionChecks']))
    (w/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(result,ensure_ascii=True),flush=True)
    raise SystemExit(0 if result['passed'] else 1)


if __name__=='__main__':main()
