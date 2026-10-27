"""Positive and deliberately wrong controls for the manufacturing checker."""
from pathlib import Path
import argparse,copy,datetime,hashlib,json,math,os,shutil,subprocess,sys
from build import Train


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--exe',type=Path,required=True);ap.add_argument('--exporter',type=Path,required=True);ap.add_argument('--work',type=Path,required=True)
    a=ap.parse_args();work=a.work.resolve();work.mkdir(parents=True,exist_ok=False)
    a.exporter=a.exporter.resolve();(work/'bin').mkdir();frozen=work/'bin'/a.exporter.name;shutil.copy2(a.exporter,frozen)
    for dll in a.exporter.parent.glob('*.dll'):shutil.copy2(dll,frozen.parent/dll.name)
    a.exporter=frozen;m=Train();g=m.group('Checker contract')
    whole=m.box('Whole',0,0,20,3,2,2,g);obstacle=m.box('Obstacle',4,0,20,1,2,2,g)
    pieces=[m.box('Piece '+str(i),i,0,20,1,2,2,g) for i in range(3)];m.entities[whole]['visibility']='hidden'
    legacy=m.box('Legacy rear piece',1,0,20,2,2,2,g);m.entities[legacy]['visibility']='hidden'
    cylinder=m.cylinder('Curved moving body',(-3,1,20),(0,0,1),.5,2,g);m.entities[cylinder]['visibility']='hidden'
    outer=m.rect('Frame outer',(-4,-1,20),(1,0,0),(0,1,0),2,4,g)
    inner=m.rect('Frame opening',(-3.8,-.8,20),(1,0,0),(0,1,0),1.6,3.6,g)
    frame=m.extrude('Frame',[outer,inner],(0,0,1),2,g);m.entities[frame]['visibility']='hidden'
    profiles=[]
    for x,z in [(0.,22.),(1.,22.1),(3.,22.)]:
        profiles.append(m.wire('Graph roof section',[m.arc((x,0,z),(1,0,0),(0,1,0),1,0,math.pi/2),
            m.arc((x,0,z),(1,0,0),(0,1,0),1,math.pi/2,math.pi/2),m.line((x,-1,z),(x,1,z))],g))
    graph=m.loft('Separable graph roof',profiles,g);m.entities[graph]['visibility']='hidden'
    graph_base=m.box('Roof obstacle',0,-2,20,3,4,2,g);m.entities[graph_base]['visibility']='hidden'
    model=work/'contract.kcd2';m.save(model)
    validator=Path(__file__).resolve().parents[1]/'series115_print/validate_model.py'
    subprocess.run([sys.executable,str(validator),str(model),str(a.exe.resolve()),'--work',str(work/'native')],check=True)
    design=dict(entities=m.d['entities'],groups=m.d['groups'],stlChordMm=.01,pointChecks=[],assemblyChecks=[],attachmentChecks=[],
        splitChecks=[dict(source=whole,parts=pieces,label='Three exact pieces')],
        insertionChecks=[dict(moving=[whole],fixed=[obstacle],vectorMm=[-5,0,0],label='Continuous separation')])
    cases=[('positive',design,0)]
    old=copy.deepcopy(design);old['splitChecks']=[dict(source=whole,front=pieces[0],rear=legacy,label='Legacy binary split')];cases.append(('legacy-two-piece',old,0))
    bad=copy.deepcopy(design);bad['splitChecks'][0]['parts']=[pieces[0],pieces[2]];cases.append(('missing-piece',bad,1))
    bad=copy.deepcopy(design);bad['insertionChecks'][0]['vectorMm']=[2,0,0];cases.append(('path-obstruction',bad,1))
    bad=copy.deepcopy(design);bad['splitChecks'][0]['parts']=[pieces[0],pieces[0]];cases.append(('duplicate-piece',bad,5))
    bad=copy.deepcopy(design);bad['splitChecks'][0]['parts']=[whole,pieces[0]];cases.append(('overlapping-different-ids',bad,1))
    bad=copy.deepcopy(design);bad['splitChecks'][0]['parts']=[];cases.append(('empty-pieces',bad,5))
    bad=copy.deepcopy(design);bad['insertionChecks'][0]['vectorMm']=[0,0,0];cases.append(('empty-path',bad,5))
    curved=copy.deepcopy(design);curved['insertionChecks'][0]['moving']=[cylinder];cases.append(('curved-safe',curved,0))
    bad=copy.deepcopy(curved);bad['insertionChecks'][0]['vectorMm']=[8,0,0];cases.append(('curved-obstructed',bad,1))
    safe=copy.deepcopy(design);safe['insertionChecks'][0].update(moving=[cylinder],fixed=[frame],vectorMm=[0,0,5]);cases.append(('nongraph-through-opening',safe,0))
    safe=copy.deepcopy(design);safe['insertionChecks'][0].update(moving=[graph],fixed=[graph_base],vectorMm=[0,0,5]);cases.append(('graph-roof',safe,0))
    bad=copy.deepcopy(safe);bad['insertionChecks'][0]['vectorMm']=[0,0,-2];cases.append(('graph-obstructed',bad,1))
    env=os.environ.copy();env['PATH']=str(a.exe.resolve().parent)+os.pathsep+env.get('PATH','');rows=[]
    for name,d,expected in cases:
        path=work/(name+'.json');path.write_text(json.dumps(d),encoding='utf-8');out=work/name
        result=subprocess.run([str(a.exporter.resolve()),str(work/'native/prepared.bin'),str(path),str(out),'--check-only'],env=env,capture_output=True,text=True,timeout=120)
        (work/(name+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode==expected,(name,result.returncode,result.stdout,result.stderr)
        report=json.loads((out/'print-parts.json').read_text(encoding='utf-8')) if (out/'print-parts.json').exists() else None
        if expected==0:
            assert report['splitChecks'][0]['passed'] and report['insertionChecks'][0]['passed'] and not report['failures']
            if name=='graph-roof':assert report['insertionChecks'][0]['graphPrismFaces']>0
            if name=='nongraph-through-opening':assert report['insertionChecks'][0]['curvedContainingBoxes']>0 and not report['insertionChecks'][0]['graphPrismFaces']
        elif name=='missing-piece':assert not report['splitChecks'][0]['passed'] and report['splitChecks'][0]['symmetricDifferenceMm3']>3.99
        elif name=='overlapping-different-ids':assert not report['splitChecks'][0]['passed'] and report['splitChecks'][0]['pieceOverlapSumMm3']>3.99
        elif name in ['path-obstruction','curved-obstructed','graph-obstructed']:assert not report['insertionChecks'][0]['passed'] and report['insertionChecks'][0]['overlapUpperBoundMm3']>1e-6
        rows.append(dict(name=name,expectedExit=expected,actualExit=result.returncode,passed=True))
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    (work/'result.json').write_text(json.dumps(dict(passed=True,cases=rows,exeSha256=sha(a.exe),exporterSha256=sha(a.exporter),
        modelSha256=sha(model),timeUtc=datetime.datetime.now(datetime.timezone.utc).isoformat()),indent=2),encoding='utf-8')
    print('PASS manufacturing checker controls:',len(rows))


if __name__=='__main__':main()
