"""Evidence-bound intermediate geometry trials; never labels a preview as final."""
from pathlib import Path
import argparse,datetime,hashlib,json,os,shutil,subprocess,sys,time

ROOT=Path(__file__).resolve().parents[3]
HERE=Path(__file__).resolve().parent


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path,data):path.write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')


def run(command,folder,name,env=None):
    start=datetime.datetime.now(datetime.timezone.utc).isoformat();tic=time.perf_counter()
    print(name,flush=True)
    with (folder/(name+'.log')).open('w',encoding='utf-8') as log:
        result=subprocess.run([str(c) for c in command],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,env=env,timeout=900)
    record=dict(command=[str(c) for c in command],startUtc=start,
        endUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),seconds=time.perf_counter()-tic,exit=result.returncode)
    write(folder/(name+'.json'),record)
    if result.returncode:raise RuntimeError(f'{name}: exit {result.returncode}; see {folder/(name+".log")}')


def main():
    ap=argparse.ArgumentParser();ap.add_argument('stage',type=int);ap.add_argument('work',type=Path)
    ap.add_argument('--exe',type=Path,required=True);ap.add_argument('--exporter',type=Path,required=True)
    ap.add_argument('--bogie',type=Path,required=True);ap.add_argument('--baseline',type=Path,required=True)
    ap.add_argument('--reuse-native',type=Path,help='Reuse only exact model/exe matching native proof, never a commit-name match')
    a=ap.parse_args();a.exe=a.exe.resolve();a.exporter=a.exporter.resolve();a.bogie=a.bogie.resolve();a.baseline=a.baseline.resolve()
    folder=a.work.resolve();folder.mkdir(parents=True,exist_ok=False)
    files=sorted(p for p in (ROOT/'tools/modeling').rglob('*') if p.suffix in ['.py','.cpp','.h','.txt'] and '__pycache__' not in p.parts)
    sources={str(p.relative_to(ROOT)).replace('\\','/'):sha(p) for p in files}
    for p in files:
        destination=folder/'sources'/p.relative_to(ROOT);destination.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,destination)
    snapshot=folder/'sources/tools/modeling';scripts=snapshot/'kuha115_ho'
    (folder/'bin').mkdir();exporter=folder/'bin/native_print_export.exe';shutil.copy2(a.exporter,exporter)
    for dll in a.exporter.parent.glob('*.dll'):shutil.copy2(dll,exporter.parent/dll.name)
    env=os.environ.copy();env['PATH']=str(a.exe.parent)+os.pathsep+env.get('PATH','')
    smoke=subprocess.run([str(exporter)],cwd=ROOT,env=env,capture_output=True,timeout=10)
    write(folder/'00-exporter-smoke.json',dict(exit=smoke.returncode,expectedExit=2,passed=smoke.returncode==2,
        dllSha256={p.name:sha(p) for p in exporter.parent.glob('*.dll')}))
    assert smoke.returncode==2,'Exporter startup/dependency smoke failed'
    write(folder/'source-provenance.json',dict(stage=a.stage,timeUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        sourceFiles=sources,exeSha256=sha(a.exe),exporterSha256=sha(a.exporter),bogieSha256=sha(a.bogie)))
    model=folder/'candidate/kuha115-ho80-revised.kcd2';design=folder/'candidate/design.json';exports=folder/'exports'
    py=sys.executable
    run([py,scripts/'revised.py','--detailed','--contoured','--appearance-pass','5','--refinement-pass',a.stage,
        '--bogie-component',a.bogie,'--output',model.parent],folder,'01-generate')
    if a.reuse_native:
        original=a.reuse_native.resolve();validation=original/'validation.json';proof=json.loads(validation.read_text(encoding='utf-8'))
        assert proof['passed'] and proof['unchanged'] and proof['modelSha']==sha(model) and proof['exeSha']==sha(a.exe)
        native=folder/'native';native.mkdir()
        for name in ['validation.json','prepared.bin']:shutil.copy2(original/name,native/name)
        write(folder/'02-native-reused.json',dict(original=str(original),validationSha256=sha(validation),
            preparedSha256=sha(original/'prepared.bin'),modelSha256=sha(model),exeSha256=sha(a.exe),token=proof['token']))
    else:run([py,snapshot/'series115_print/validate_model.py',model,a.exe,'--work',folder/'native'],folder,'02-native')
    run([exporter,folder/'native/prepared.bin',design,exports,'--preview-only'],folder,'03-export',env)
    native_proof=json.loads((folder/'native/validation.json').read_text(encoding='utf-8'))
    native_report=json.loads((exports/'print-parts.json').read_text(encoding='utf-8'))
    definitions=json.loads(design.read_text(encoding='utf-8'))
    expected={e['id'] for e in definitions['entities'] if e['kind']=='part' and e['visibility']=='visible'}
    assert native_proof['passed'] and native_proof['unchanged'] and native_proof['modelSha']==sha(model)
    assert native_report['preparedToken']==native_proof['token'] and not native_report['failures']
    assert native_report['previewOnly'] and not native_report['checkOnly']
    assert len(expected)==native_report['visibleParts']==len(native_report['parts']) and expected=={p['id'] for p in native_report['parts']}
    for key in ['pointChecks','assemblyChecks','attachmentChecks']:
        assert len(definitions[key])==len(native_report[key])>0
        assert all(row['passed'] and all(row[k]==v for k,v in source.items()) for source,row in zip(definitions[key],native_report[key]))
    assert len(native_report['pointChecks'])>=1000 and len(native_report['attachmentChecks'])>=100
    run([py,snapshot/'series115_print/check_stl.py',exports],folder,'04-stl')
    run([py,scripts/'inspect_refinement.py',exports,folder/'inspection.json','--baseline',a.baseline],folder,'05-seam')
    render_command=[py,scripts/'render_detailed.py',exports,design,folder/'previews','--diagnostic-front'] if a.stage<=5 else [py,scripts/'render_refinement.py',exports,design,folder/'previews']
    run(render_command,folder,'06-render')
    for name,digest in sources.items():assert sha(folder/'sources'/name)==digest,f'Frozen source changed: {name}'
    provenance=json.loads((folder/'source-provenance.json').read_text(encoding='utf-8'))
    assert sha(exporter)==provenance['exporterSha256'] and sha(a.exe)==provenance['exeSha256']
    proof={str(p.relative_to(folder)).replace('\\','/'):sha(p) for p in [model,design,folder/'native/validation.json',
        exports/'print-parts.json',exports/'stl-validation.json',folder/'inspection.json',folder/'previews/render-proof.json']}
    write(folder/'result.json',dict(stage=a.stage,geometryPassed=True,visualReview='pending',finalPrintRelease=False,
        modelSha256=sha(model),evidence=proof,endUtc=datetime.datetime.now(datetime.timezone.utc).isoformat()))
    print('GEOMETRY PASSED; image review pending',flush=True)


if __name__=='__main__':main()
