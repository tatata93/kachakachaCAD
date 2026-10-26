"""Reject detached fittings with real BRep distance/area checks, without editing CAD."""
from pathlib import Path
import argparse,copy,hashlib,json,subprocess,time


def main():
    ap=argparse.ArgumentParser();ap.add_argument('prepared',type=Path);ap.add_argument('design',type=Path)
    ap.add_argument('exporter',type=Path);ap.add_argument('--work',required=True,type=Path);a=ap.parse_args()
    a.work.mkdir(parents=True,exist_ok=False);design=json.loads(a.design.read_text(encoding='utf-8'))
    assert design['attachmentChecks'];results=[]
    def check(name,source,delta):
        d=copy.deepcopy(design);row=next(x for x in d['attachmentChecks'] if source in x['label'])
        row['secondTranslationMm']=delta;d['attachmentChecks']=[row]
        d['assemblyChecks']=[];d['pointChecks']=[];d['splitChecks']=[]
        output=a.work/name;path=a.work/(name+'.json');path.write_text(json.dumps(d,ensure_ascii=False),encoding='utf-8')
        started=time.monotonic()
        with (a.work/(name+'.log')).open('w',encoding='utf-8') as log:
            p=subprocess.run([str(a.exporter.resolve()),str(a.prepared.resolve()),str(path.resolve()),str(output.resolve()),'--check-only'],stdout=log,stderr=subprocess.STDOUT,timeout=600)
        r=json.loads((output/'print-parts.json').read_text(encoding='utf-8'))
        assert p.returncode==1 and r['failures']==1 and len(r['attachmentChecks'])==1
        actual=r['attachmentChecks'][0];assert not actual['passed']
        assert actual['distanceMm']>row['maximumDistanceMm'] or actual['contactAreaMm2']<row['minimumAreaMm2']
        results.append(dict(case=name,translationMm=delta,seconds=time.monotonic()-started,result=actual))
    # Move the second shape away from the first fitting's mating surface.
    check('skirt-gap','スカート左右',[-.5,0,0])
    check('glazing-gap','側窓ガラス',[0,-.2,0])
    check('cooler-gap','冷房サドル',[0,0,-.5])
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    proof=dict(passed=True,preparedSha256=sha(a.prepared),designSha256=sha(a.design),exporterSha256=sha(a.exporter),cases=results)
    (a.work/'results.json').write_text(json.dumps(proof,ensure_ascii=False,indent=2),encoding='utf-8')
    print('Rejected detached skirt, glazing and cooler:',len(results))


if __name__=='__main__':main()
