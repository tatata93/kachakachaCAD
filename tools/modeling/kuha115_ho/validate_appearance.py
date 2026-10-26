"""Final native model gate; split export can run beside the last visual review."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,subprocess,sys,time
from review import extract_roundtrip


def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('exporter',type=Path);ap.add_argument('cad',type=Path)
    ap.add_argument('--phase',choices=['split','finish'],required=True);a=ap.parse_args()
    w=a.workspace.resolve();exporter=a.exporter.resolve();exe=a.cad.resolve();candidate=w/'candidate05'
    repo=Path(__file__).resolve().parents[3];model=Path(__file__).resolve().parent;native=model.parent/'series115_print'
    env=os.environ.copy();env['PATH']=str(exe.parent)+os.pathsep+env['PATH'];env['PYTHONIOENCODING']='utf-8'
    read=lambda p:json.loads(p.read_text(encoding='utf-8'));sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    def run(label,args,timeout=1200):
        start=time.time();stamp=datetime.datetime.now(datetime.timezone.utc).isoformat();print('START',label,flush=True)
        receipt=w/(label+'-process.json');assert not receipt.exists(),'Do not silently overwrite earlier attempts'
        with (w/(label+'.log')).open('w',encoding='utf-8') as log:
            p=subprocess.run([str(x) for x in args],env=env,cwd=repo,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
        receipt.write_text(json.dumps(dict(startUtc=stamp,endUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
            seconds=time.time()-start,exitCode=p.returncode,command=[str(x) for x in args])),encoding='utf-8',newline='\n')
        print('END',label,p.returncode,round(time.time()-start,2),flush=True)
        if p.returncode:raise RuntimeError(label+' failed; see retained log')
    split=a.phase=='split';prefix='split-' if split else '';suffix='-split' if split else ''
    check=w/(prefix+'native05');exports=w/(prefix+'exports05');design=candidate/('design'+suffix+'.json')
    v=read(check/'validation.json');d=read(design)
    assert v['passed'] and v['unchanged'] and v['exeSha']==sha(exe) and v['modelSha']==sha(candidate/('kuha115-ho80-revised'+suffix+'.kcd2'))
    if split:
        assert not exports.exists();run(prefix+'export05',[exporter,check/'prepared.bin',design,exports])
    else:
        # Reuse only the exact complete native export made for visual pass 5.
        r=read(exports/'print-parts.json');assert r['preparedToken']==v['token'] and not r['failures'] and not r['checkOnly']
        for key in ['pointChecks','assemblyChecks','attachmentChecks','splitChecks']:
            assert len(r[key])==len(d.get(key,[])) and all(c['passed'] for c in r[key])
            assert all(all(actual.get(k)==value for k,value in expected.items()) for expected,actual in zip(d.get(key,[]),r[key]))
        assert r['visibleParts']==len(r['parts'])>200 and all(p['exported'] and p['stepRoundTrip'] for p in r['parts'])
    run(prefix+'stl05',[sys.executable,native/'check_stl.py',exports])
    all_design=candidate/('design'+suffix+'-all-pairs.json')
    run(prefix+'candidates05',[sys.executable,model/'assembly_candidates.py',exports/'print-parts.json',design,all_design])
    run(prefix+'all-pairs05',[exporter,check/'prepared.bin',all_design,w/(prefix+'all-pairs05'),'--check-only'])
    if split:return
    for pre in ['', 'split-']:extract_roundtrip(w/(pre+'native05/prepared.bin'),w/(pre+'native05/cad-serialized.kcd2'))
    run('reopen05',[sys.executable,native/'validate_model.py',w/'native05/cad-serialized.kcd2',exe,'--work',w/'reopen05'])
    run('reopen-geometry05',[exporter,w/'reopen05/prepared.bin',design,w/'reopen-geometry05','--check-only'])
    # Normal desktop startup is the requirement. A worker cache is an optional
    # performance optimization and must not be assumed to survive serialization.
    run('desktop05',[sys.executable,model/'desktop_model.py',w/'native05/cad-serialized.kcd2',exe,'--work',w/'desktop05'])
    run('review05',[sys.executable,model/'review_revised.py',w,'--run','05'])
    run('negative-review05',[sys.executable,model/'test_revised_review.py',w,'--run','05'])
    run('appearance-sections05',[sys.executable,model/'audit_appearance_geometry.py',repo/'out/models/kuha115-contour-review-20261026',w])
    print('PASS final native appearance gate',flush=True)


if __name__=='__main__':main()
