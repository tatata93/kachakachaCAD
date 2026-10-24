"""Conservative AABB broad phase; the OCCT checker then tests actual solids."""
from pathlib import Path
import argparse,itertools,json


def main():
    ap=argparse.ArgumentParser();ap.add_argument('report',type=Path);ap.add_argument('design',type=Path);ap.add_argument('output',type=Path)
    a=ap.parse_args();r=json.loads(a.report.read_text(encoding='utf-8'));d=json.loads(a.design.read_text(encoding='utf-8'))
    assert r['visibleParts']==len(r['parts'])>0 and all(p['valid'] for p in r['parts'])
    visible={e['id'] for e in d['entities'] if e['kind']=='part' and e['visibility']=='visible'}
    assert visible=={p['id'] for p in r['parts']}
    checks=[]
    for first,second in itertools.combinations(r['parts'],2):
        if all(min(first['bounds'][i+3],second['bounds'][i+3])-max(first['bounds'][i],second['bounds'][i])>=-1e-5 for i in range(3)):
            checks.append(dict(first=first['id'],second=second['id'],label=first['name']+' / '+second['name'],noOverlap=True))
    assert checks
    d['assemblyChecks']=checks
    d['broadPhase']=dict(method='CAD BRep AABB gaps <= 1e-5 mm on every axis (expanded bounds)',
                         potentialPairs=len(r['parts'])*(len(r['parts'])-1)//2,candidates=len(checks),
                         narrowPhase='OCCT Common volume <= 1e-6 mm3, no exemptions')
    a.output.write_text(json.dumps(d,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    print(json.dumps(d['broadPhase']))


if __name__=='__main__':main()
