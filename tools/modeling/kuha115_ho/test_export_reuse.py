"""Reject mismatched, missing or failed evidence using real delivery receipts."""
from pathlib import Path
import argparse,copy,json,os,shutil
from reuse_exports import reuse,sha,load


def main():
    ap=argparse.ArgumentParser();ap.add_argument('previous',type=Path);ap.add_argument('paths',type=Path);ap.add_argument('model',type=Path)
    ap.add_argument('design',type=Path);ap.add_argument('native',type=Path);ap.add_argument('work',type=Path);a=ap.parse_args()
    a.work.mkdir(parents=True,exist_ok=False);results=[]
    for case in ['stale-model','missing-part','zero-paths','failed-path','wrong-token','changed-bounds','failed-point','unbound-step','changed-step','changed-stl']:
        w=a.work/case;p=w/'previous';q=w/'paths';n=w/'native';(p/'exports').mkdir(parents=True);(q/'exports').mkdir(parents=True);(q/'bin').mkdir();n.mkdir()
        for name in ['validation.json','prepared.bin']:os.link(a.native/name,n/name)
        old=load(a.previous/'exports/print-parts.json');provenance=load(a.previous/'provenance.json');new=load(a.paths/'exports/print-parts.json');receipt=load(a.paths/'result.json')
        for name in ['exporter-source.cpp']:shutil.copy2(a.paths/name,q/name)
        os.link(a.paths/'bin/native_print_export.exe',q/'bin/native_print_export.exe')
        if case=='stale-model':provenance['modelSha256']='0'*64
        elif case=='missing-part':new['parts'].pop();new['visibleParts']-=1
        elif case=='zero-paths':new['insertionChecks']=[]
        elif case=='failed-path':new['insertionChecks'][0]['passed']=False;new['failures']=1;receipt['passed']=False
        elif case=='wrong-token':new['preparedToken']='deliberately-wrong-token'
        elif case=='changed-bounds':new['parts'][0]['bounds'][0]+=.1
        elif case=='failed-point':old['pointChecks'][0]['passed']=False
        elif case in ['unbound-step','changed-step','changed-stl']:
            assert all(p.get('stlSha256') and p.get('stepSha256') for p in old['parts'])
            if case=='unbound-step':old['parts'][0].pop('stepSha256')
            else:
                kind='step' if case=='changed-step' else 'stl'
                source=a.previous/'exports'/old['parts'][0][kind]
                # Mutate a private copy; never a link to accepted evidence.
                changed=p/'exports'/old['parts'][0][kind];shutil.copy2(source,changed);changed.write_bytes(b'changed-after-verification')
                other='stl' if kind=='step' else 'step';os.link(a.previous/'exports'/old['parts'][0][other],p/'exports'/old['parts'][0][other])
            if case=='unbound-step':os.link(a.previous/'exports'/old['parts'][0]['stl'],p/'exports'/old['parts'][0]['stl'])
        for path,data in [(p/'provenance.json',provenance),(p/'exports/print-parts.json',old),(q/'exports/print-parts.json',new)]:path.write_text(json.dumps(data),encoding='utf-8')
        receipt['reportSha256']=sha(q/'exports/print-parts.json');(q/'result.json').write_text(json.dumps(receipt),encoding='utf-8')
        try:reuse(p,q,a.model,a.design,n,w/'unexpected-output')
        except AssertionError as error:
            if case=='unbound-step':assert 'Unbound legacy' in str(error)
            elif case in ['changed-step','changed-stl']:assert 'Export changed' in str(error)
            results.append(dict(case=case,rejected=True,reason=str(error)))
        else:raise AssertionError('Invalid evidence was accepted: '+case)
    (a.work/'result.json').write_text(json.dumps(dict(passed=True,cases=results),indent=2),encoding='utf-8');print('PASS evidence rejection cases',len(results))


if __name__=='__main__':main()
