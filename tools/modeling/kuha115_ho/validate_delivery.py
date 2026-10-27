"""Final manufacturing evidence: full exports, all pairs and native save/reopen."""
from pathlib import Path
import argparse,copy,datetime,hashlib,itertools,json,os,shutil,subprocess,sys,time
from review import extract_roundtrip
from review_revised import identity,archive


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p):return json.loads(p.read_text(encoding='utf-8'))
def write(p,d):p.write_text(json.dumps(d,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')


def main():
    ap=argparse.ArgumentParser();ap.add_argument('model',type=Path);ap.add_argument('design',type=Path);ap.add_argument('native',type=Path)
    ap.add_argument('exe',type=Path);ap.add_argument('exporter',type=Path);ap.add_argument('work',type=Path)
    ap.add_argument('--reuse-full-export',type=Path);ap.add_argument('--replacement-paths',type=Path);ap.add_argument('--resume',action='store_true');a=ap.parse_args()
    assert bool(a.reuse_full_export)==bool(a.replacement_paths),'Both evidence paths are required'
    model,design,native,exe,exporter,w=[getattr(a,k).resolve() for k in ['model','design','native','exe','exporter','work']]
    if a.resume:assert w.is_dir() and load(w/'04-all-pairs.json')['exit']==0,'Only successful completed geometry stages can resume'
    else:w.mkdir(parents=True,exist_ok=False)
    root=Path(__file__).resolve().parents[3];snapshot=w/'sources/tools/modeling'
    files=[p for p in (root/'tools/modeling').rglob('*') if p.suffix in ['.py','.cpp','.h','.txt'] and '__pycache__' not in p.parts]
    if not a.resume:
        for p in files:
            t=w/'sources'/p.relative_to(root);t.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,t)
        (w/'bin').mkdir();frozen=w/'bin/native_print_export.exe';shutil.copy2(exporter,frozen)
        for p in exporter.parent.glob('*.dll'):shutil.copy2(p,frozen.parent/p.name)
    else:
        frozen=w/'bin/native_print_export.exe';resume=w/'resume-source.py';assert not resume.exists();shutil.copy2(Path(__file__),resume)
    env=os.environ.copy();env['PATH']=str(exe.parent)+os.pathsep+env.get('PATH','')
    provenance=dict(modelSha256=sha(model),designSha256=sha(design),exeSha256=sha(exe),exporterSha256=sha(frozen),
        nativeProofSha256=sha(native/'validation.json'),preparedSha256=sha(native/'prepared.bin'),
        sources={str(p.relative_to(root)).replace('\\','/'):sha(p) for p in files})
    if a.resume:
        existing=load(w/'provenance.json')
        for key in ['modelSha256','designSha256','exeSha256','exporterSha256','nativeProofSha256','preparedSha256']:assert existing[key]==provenance[key],key
        provenance=existing
    else:write(w/'provenance.json',provenance)
    proof=load(native/'validation.json');definitions=load(design)
    assert proof['passed'] and proof['unchanged'] and proof['modelSha']==sha(model) and proof['exeSha']==sha(exe)
    def run(cmd,name):
        print(name,flush=True);start=datetime.datetime.now(datetime.timezone.utc).isoformat();tic=time.monotonic()
        with (w/(name+'.log')).open('w',encoding='utf-8') as log:r=subprocess.run(list(map(str,cmd)),env=env,stdout=log,stderr=subprocess.STDOUT,timeout=1200)
        write(w/(name+'.json'),dict(command=list(map(str,cmd)),startUtc=start,endUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),seconds=time.monotonic()-tic,exit=r.returncode))
        assert r.returncode==0,f'{name}: exit {r.returncode}'
    exports=w/'exports';py=sys.executable
    if a.resume:assert load(exports/'print-parts.json')['preparedToken']==proof['token']
    elif a.reuse_full_export:
        from reuse_exports import reuse
        reuse(a.reuse_full_export.resolve(),a.replacement_paths.resolve(),model,design,native,exports)
        write(w/'01-reused-exports.json',dict(previous=str(a.reuse_full_export.resolve()),replacement=str(a.replacement_paths.resolve()),
            receiptSha256=sha(exports/'reuse-evidence.json')))
    else:run([frozen,native/'prepared.bin',design,exports],'01-full-export')
    r=load(exports/'print-parts.json');expected={e['id'] for e in definitions['entities'] if e['kind']=='part' and e['visibility']=='visible'}
    assert not r['failures'] and not r['checkOnly'] and not r['previewOnly'] and r['preparedToken']==proof['token']
    assert expected=={p['id'] for p in r['parts']} and len(expected)==r['visibleParts']>0
    for key in ['pointChecks','assemblyChecks','attachmentChecks','splitChecks','insertionChecks']:
        assert len(definitions.get(key,[]))==len(r.get(key,[]))
        assert all(row['passed'] and all(row[k]==v for k,v in source.items()) for source,row in zip(definitions.get(key,[]),r.get(key,[])))
    assert all(p['valid'] and p['solids']==1 and p['volumeMm3']>0 and p['exported'] and p['stepRoundTrip'] for p in r['parts'])
    if not a.resume:run([py,snapshot/'series115_print/check_stl.py',exports],'02-stl')
    else:assert not load(exports/'stl-validation.json')['failures'] and load(exports/'stl-validation.json')['parts']==len(expected)
    all_design=w/'all-pairs-design.json'
    if not a.resume:
        run([py,snapshot/'kuha115_ho/assembly_candidates.py',exports/'print-parts.json',design,all_design],'03-pair-selection')
        run([frozen,native/'prepared.bin',all_design,w/'all-pairs','--check-only'],'04-all-pairs')
    ad,ar=load(all_design),load(w/'all-pairs/print-parts.json')
    expected_pairs={frozenset((p['id'],q['id'])) for p,q in itertools.combinations(r['parts'],2)
        if all(min(p['bounds'][i+3],q['bounds'][i+3])-max(p['bounds'][i],q['bounds'][i])>=-1e-5 for i in range(3))}
    assert not ar['failures'] and ar['checkOnly'] and ar['preparedToken']==proof['token']
    assert expected_pairs=={frozenset((c['first'],c['second'])) for c in ar['assemblyChecks']}
    receipt=dict(reportSha256=sha(exports/'print-parts.json'),preparedToken=proof['token'],pointChecks=len(r['pointChecks']),splitChecks=len(r['splitChecks']))
    if definitions.get('attachmentRevision'):receipt['attachmentChecks']=len(r['attachmentChecks'])
    if definitions.get('insertionChecks'):receipt['insertionChecks']=len(r['insertionChecks'])
    assert receipt==ad['broadPhase']['reusedChecks'] and len(expected_pairs)==ad['broadPhase']['candidates']>0
    saved=w/'cad-serialized.kcd2';token,doc=extract_roundtrip(native/'prepared.bin',saved);assert token==proof['token']
    comparison=archive(model);normalized=[]
    if definitions.get('kit'):
        # Generated parts were reparented after creation. The CAD registers
        # entities in entity-array order, then stores that canonical order.
        # Record that source normalization; never call it preserved raw order.
        for group in comparison['groups']:
            ids=[e['id'] for e in comparison['entities'] if e['groupId']==group['id']]
            groups={g['id'] for g in comparison['groups']};children=[i for i in group['childOrder'] if i in groups]
            old=[i for i in group['childOrder'] if i not in groups];assert len(old)==len(ids) and set(old)==set(ids)
            if old!=ids:normalized.append(dict(groupId=group['id'],sourceOrder=old,canonicalOrder=ids))
            group['childOrder']=children+ids
    identity(comparison,doc)
    write(w/'serialization-order.json',dict(rawSourceOrderPreserved=not normalized,normalizedGroups=normalized,
        finalArchiveSha256=sha(saved),reason='Publish the CAD-written canonical archive; verify that this order survives reopening'))
    run([py,snapshot/'series115_print/validate_model.py',saved,exe,'--work',w/'reopen'],'05-reopen')
    reopen=load(w/'reopen/validation.json');assert reopen['passed'] and reopen['modelSha']==sha(saved) and reopen['exeSha']==sha(exe)
    # Reuse already-bound probes; this separate pass compares every saved shape.
    rd=copy.deepcopy(definitions)
    for key in ['pointChecks','assemblyChecks','attachmentChecks','splitChecks','insertionChecks']:rd[key]=[]
    write(w/'reopen-design.json',rd)
    run([frozen,w/'reopen/prepared.bin',w/'reopen-design.json',w/'reopen-geometry','--check-only'],'06-reopen-geometry')
    reopened=load(w/'reopen-geometry/print-parts.json');assert not reopened['failures'] and reopened['preparedToken']==reopen['token']
    parts={p['id']:p for p in reopened['parts']};assert expected==parts.keys()
    delta=max(abs(p['volumeMm3']-parts[p['id']]['volumeMm3']) for p in r['parts']);assert delta<1e-6
    assert all(abs(p['bounds'][i]-parts[p['id']]['bounds'][i])<1e-6 for p in r['parts'] for i in range(6))
    reopened_archive=w/'reopened-cad-serialized.kcd2';reopened_token,reopened_doc=extract_roundtrip(w/'reopen/prepared.bin',reopened_archive)
    assert reopened_token==reopen['token'];identity(comparison,reopened_doc)
    assert doc['groups']==reopened_doc['groups'],'Canonical group order must survive reopening exactly'
    for path,digest in provenance['sources'].items():assert sha(w/'sources'/path)==digest
    assert sha(model)==provenance['modelSha256'] and sha(design)==provenance['designSha256'] and sha(frozen)==provenance['exporterSha256']
    write(w/'result.json',dict(passed=True,parts=len(expected),pointChecks=len(r['pointChecks']),attachmentChecks=len(r['attachmentChecks']),
        splitChecks=len(r['splitChecks']),insertionChecks=len(r['insertionChecks']),totalPairs=len(expected)*(len(expected)-1)//2,
        brepPairs=len(expected_pairs),maximumReopenVolumeDeltaMm3=delta,provenanceSha256=sha(w/'provenance.json'),
        reportSha256=sha(exports/'print-parts.json'),stlProofSha256=sha(exports/'stl-validation.json'),
        canonicalArchiveSha256=sha(saved),sourceOrderNormalizedGroups=len(normalized),canonicalOrderPreserved=True,
        resumeSourceSha256=sha(w/'resume-source.py') if a.resume else None,timeUtc=datetime.datetime.now(datetime.timezone.utc).isoformat()))
    print('FINAL GEOMETRY/SAVE GATES PASSED; desktop and visual review recorded separately',flush=True)


if __name__=='__main__':main()
