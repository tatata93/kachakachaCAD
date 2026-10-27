"""Reuse full file exports while replacing only independently rechecked paths.

The original failed aggregate is retained and identified. Its successful scopes
are usable only for exactly matching native/model/design bytes and part geometry.
"""
from pathlib import Path
import copy,hashlib,json,shutil


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def load(p):return json.loads(p.read_text(encoding='utf-8'))


def reuse(previous,paths,model,design,native,target):
    previous,paths=Path(previous),Path(paths);provenance=load(previous/'provenance.json');proof=load(native/'validation.json');d=load(design)
    base=previous/'exports';old=load(base/'print-parts.json');new=load(paths/'exports/print-parts.json');receipt=load(paths/'result.json')
    assert provenance['modelSha256']==proof['modelSha']==sha(model) and provenance['designSha256']==sha(design)
    assert provenance['nativeProofSha256']==sha(native/'validation.json') and provenance['preparedSha256']==sha(native/'prepared.bin')
    assert proof['passed'] and proof['unchanged'] and provenance['exeSha256']==proof['exeSha']
    assert receipt['passed'] and receipt['exit']==0 and receipt['originalDesignSha256']==sha(design) and receipt['nativeProofSha256']==sha(native/'validation.json')
    assert receipt['reportSha256']==sha(paths/'exports/print-parts.json')
    assert receipt['exporterSha256']==sha(paths/'bin/native_print_export.exe') and receipt['sourceSha256']==sha(paths/'exporter-source.cpp')
    assert old['preparedToken']==new['preparedToken']==proof['token'] and new['checkOnly'] and not old['checkOnly'] and not old['previewOnly']
    for key in ['pointChecks','assemblyChecks','attachmentChecks','splitChecks']:
        assert len(old[key])==len(d.get(key,[])) and not new[key]
        assert all(c['passed'] and all(c[k]==v for k,v in source.items()) for source,c in zip(d.get(key,[]),old[key]))
    assert len(d['insertionChecks'])==len(new['insertionChecks'])>0
    assert all(c['complete'] and c['passed'] and all(c[k]==v for k,v in source.items()) for source,c in zip(d['insertionChecks'],new['insertionChecks']))
    assert old['failures']==sum(not c['passed'] for c in old['insertionChecks']) and not new['failures']
    expected={e['id'] for e in d['entities'] if e['kind']=='part' and e['visibility']=='visible'};actual={p['id']:p for p in new['parts']}
    assert expected=={p['id'] for p in old['parts']}==actual.keys() and len(expected)==old['visibleParts']==new['visibleParts']>0
    for p in old['parts']:
        q=actual[p['id']];assert p['valid'] and p['solids']==1 and p['exported'] and p['stepRoundTrip'] and q['valid'] and q['solids']==1
        assert abs(p['volumeMm3']-q['volumeMm3'])<1e-8 and all(abs(a-b)<1e-9 for a,b in zip(p['bounds'],q['bounds']))
        assert p['chordMm']==d['stlChordByEntity'].get(p['id'],d['stlChordMm'])
        # Bind the files to the digests recorded immediately after the native
        # export/STEP reread. Never create a fresh digest to bless old bytes.
        for key in ['stl','step']:
            assert len(p.get(key+'Sha256',''))==64,'Unbound legacy exports cannot be reused'
            assert sha(base/p[key])==p[key+'Sha256'],'Export changed after its validation'
    target.mkdir(parents=True,exist_ok=False);result=copy.deepcopy(old);result['insertionChecks']=new['insertionChecks'];result['failures']=0
    result['verificationSegments']=dict(fullReportSha256=sha(base/'print-parts.json'),fullProvenanceSha256=sha(previous/'provenance.json'),
        originalAggregateFailures=old['failures'],replacedScope='insertionChecks',pathReportSha256=sha(paths/'exports/print-parts.json'),pathReceiptSha256=sha(paths/'result.json'))
    for p in result['parts']:
        for key in ['stl','step']:
            source=base/p[key];shutil.copy2(source,target/p[key]);digest=sha(source);assert sha(target/p[key])==digest;p[key+'Sha256']=digest
    (target/'print-parts.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    (target/'reuse-evidence.json').write_text(json.dumps(result['verificationSegments'],indent=2),encoding='utf-8')
