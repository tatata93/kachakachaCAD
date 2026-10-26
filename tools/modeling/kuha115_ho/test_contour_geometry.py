"""The rejected previous geometry must fail the NEW physical section probes."""
from pathlib import Path
import argparse,copy,hashlib,json,subprocess


def main():
    ap=argparse.ArgumentParser();ap.add_argument('baseline',type=Path)
    ap.add_argument('candidate',type=Path);ap.add_argument('exporter',type=Path)
    ap.add_argument('--work',type=Path,required=True);ap.add_argument('--run',default='03');a=ap.parse_args()
    a.work.mkdir(parents=True,exist_ok=False)
    read=lambda p:json.loads(p.read_text(encoding='utf-8'))
    old=read(a.baseline/'candidate03/design.json');new=read(a.candidate/('candidate'+a.run)/'design.json')
    positive=read(a.candidate/('exports'+a.run)/'print-parts.json')
    assert not positive['failures'] and positive['visibleParts']==232
    points=[c for c in positive['pointChecks'] if '裾絞り扉' in c['label'] or 'キセ下縁の連続' in c['label'] or '床板とスカートの連続' in c['label']]
    assert len(points)==459 and all(c['passed'] for c in points)
    design=copy.deepcopy(old);checks=[]
    old_entities={e['id']:e for e in old['entities']}
    cooler=next(e['id'] for e in old['entities'] if e['displayName']=='AU75G脚底を屋根に合わせる')
    floor=next(e['id'] for e in old['entities'] if e['displayName']=='床板・前端台枠一体')
    point=next(c for c in new['pointChecks'] if c['label']=='キセ下縁の連続・屋根直上' and abs(c['point'][0]-121.)<2 and c['point'][1]==12.)
    checks.append(dict(point,entity=cooler,label='old cooler has an exposed gap'))
    point=next(c for c in new['pointChecks'] if c['label']=='床板とスカートの連続返し' and c['point'][0]==2.5)
    checks.append(dict(point,entity=floor,label='old apron has no continuous return'))
    for section,entity in zip(new['contours']['doorSections'],old['keyParts']['doors']):
        assert '扉' in old_entities[entity]['displayName']
        point=next(c for c in new['pointChecks'] if c['entity']==section['entity'] and c['label']=='裾絞り扉の旧平板位置は空間')
        checks.append(dict(point,entity=entity,label='old door is flat '+str(section['side'])+' '+str(section['x'])))
    assert len(checks)==16
    design.update(pointChecks=checks,assemblyChecks=[],attachmentChecks=[],splitChecks=[])
    path=a.work/'old-geometry-new-probes.json';path.write_text(json.dumps(design,ensure_ascii=False,indent=2),encoding='utf-8')
    output=a.work/'old-rejected'
    with (a.work/'old-rejected.log').open('w',encoding='utf-8') as log:
        p=subprocess.run([str(a.exporter.resolve()),str((a.baseline/'native03/prepared.bin').resolve()),str(path.resolve()),str(output.resolve()),'--check-only'],stdout=log,stderr=subprocess.STDOUT,timeout=600)
    report=read(output/'print-parts.json')
    assert p.returncode==1 and report['failures']==16 and len(report['pointChecks'])==16
    assert all(not c['passed'] for c in report['pointChecks'])
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    result=dict(passed=True,newPositiveSectionProbes=len(points),rejectedOldGeometryProbes=report['pointChecks'],
        baselinePreparedSha256=sha(a.baseline/'native03/prepared.bin'),candidateDesignSha256=sha(a.candidate/('candidate'+a.run)/'design.json'),
        candidateExportReportSha256=sha(a.candidate/('exports'+a.run)/'print-parts.json'),exporterSha256=sha(a.exporter))
    (a.work/'results.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print('PASS: new sections accepted; old cooler, apron and all 14 flat doors rejected')


if __name__=='__main__':main()
