"""Focused native export and identical views for one cumulative appearance pass."""
from pathlib import Path
import argparse,copy,hashlib,json,os,subprocess,sys,time
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'series115_print'))
from render_preview import render


def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('exporter',type=Path)
    ap.add_argument('cad',type=Path);ap.add_argument('--pass-number',required=True,type=int)
    ap.add_argument('--variant',choices=['','b','c'],default='')
    ap.add_argument('--final',action='store_true',help='Pass 5: export all contacts once for the following full model gate');a=ap.parse_args()
    w=a.workspace.resolve();n=f'{a.pass_number:02}'+a.variant;candidate=w/('candidate'+n);native=w/('native'+n)
    output=w/('inspection'+n);output.mkdir(exist_ok=False)
    read=lambda p:json.loads(p.read_text(encoding='utf-8'));sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    design=read(candidate/'design.json');validation=read(native/'validation.json')
    assert validation['passed'] and validation['modelSha']==sha(candidate/'kuha115-ho80-revised.kcd2')
    assert validation['exeSha']==sha(a.cad) and design['appearance']['passNumber']==a.pass_number
    focused=copy.deepcopy(design)
    # Final delivery repeats every contact and all-pair interference. During
    # appearance iteration, keep all part validity/point/specified assembly
    # checks and choose only contacts touched by this pass.
    focused['attachmentChecks']=[c for c in design['attachmentChecks'] if
        (a.pass_number==2 and ('前面手掛け' in c['label'] or '前面ステップ' in c['label'])) or
        (a.pass_number==3 and ('客扉' in c['label'])) or
        (a.pass_number==4 and ('側窓' in c['label'] or '側面行先' in c['label'])) or
        (a.pass_number==5 and 'ジャンパ曲管' in c['label'])]
    if a.final:
        assert a.pass_number==5 and not a.variant
        focused=copy.deepcopy(design)
    exports=w/('exports'+n) if a.final else output/'exports'
    assert not exports.exists()
    manifest=output/'inspection-design.json';manifest.write_text(json.dumps(focused,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    env=os.environ.copy();env['PATH']=str(a.cad.resolve().parent)+os.pathsep+env['PATH'];start=time.time()
    with (output/'native-export.log').open('w',encoding='utf-8') as log:
        p=subprocess.run([str(a.exporter.resolve()),str(native/'prepared.bin'),str(manifest),str(exports)],env=env,stdout=log,stderr=subprocess.STDOUT,timeout=900)
    report=read(exports/'print-parts.json')
    assert p.returncode==0 and not report['failures'] and report['visibleParts']==len(report['parts'])>200
    assert report['preparedToken']==validation['token']
    for key in ['pointChecks','assemblyChecks','attachmentChecks']:
        assert len(report[key])==len(focused[key]) and all(c['passed'] for c in report[key])
        assert all(all(y.get(k)==v for k,v in x.items()) for x,y in zip(focused[key],report[key]))
    options=dict(paint_names=('乗務員扉','シャッター','前面横手掛け'),roof_forehead_mm=8.5,
        paint_bands=[(-2,22.4,(224,229,219)),(22.4,24.,(90,174,151)),(24.,38.5,(110,154,184)),(38.5,60,(224,229,219))],studio=True)
    for label,camera,box,size in [
        ('front-profile',(0,-1,0),[-4,-20,5,28,20,49],(1000,1000)),
        ('front-oblique',(-.94,-.26,.17),[-4,-20,5,28,20,49],(1100,1100)),
        ('door-side',(-.2,-1,.1),[41,-20,14.,62,-14.,40],(1000,950)),
        ('rear-side',(0,-1,0),[211,-20,13,244,-14,42],(1100,900))]:
        render(exports,report,camera=camera,clip_bounds=box,size=size,**options).save(output/(label+'.png'))
        print(label,flush=True)
    source=Path(__file__).resolve().parent
    result=dict(passed=True,scope='Final native export; mesh/all-pairs/save gates pending.' if a.final else 'Focused intermediate appearance inspection; not the final full model gate.',
        passNumber=a.pass_number,sourceModelSha256=validation['modelSha'],designSha256=sha(candidate/'design.json'),
        cadExeSha256=validation['exeSha'],preparedToken=validation['token'],nativeSeconds=validation['seconds'],seconds=time.time()-start,
        parts=len(report['parts']),points=len(report['pointChecks']),contacts=len(report['attachmentChecks']),
        specifiedInterferenceChecks=len(report['assemblyChecks']),
        sourceFilesSha256={p.name:sha(p) for p in source.glob('*.py')})
    (output/'result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    print('PASS focused appearance',a.pass_number,flush=True)


if __name__=='__main__':main()
