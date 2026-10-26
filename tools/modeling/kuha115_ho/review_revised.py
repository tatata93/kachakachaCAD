"""Fail closed on native/export/mesh, interference, saving and desktop evidence."""
from pathlib import Path
import argparse, copy, datetime, itertools, json, math, zipfile, hashlib
from review import extract_roundtrip, same_defined_values, sha, load


def archive(path):
    with zipfile.ZipFile(path) as z:return json.loads(z.read('document.json'))


def canonical_definition(feature):
    definition=copy.deepcopy(feature['definition'])
    if feature['type']=='create_wire':
        for segment in definition['wire']['segments']:
            if segment['type']!='circle':continue
            # A full circle has no physical phase. The CAD's CurveSegment
            # constructor projects its reference axis into the normal plane.
            n=[segment['normal'][k] for k in 'xyz'];x=[segment['xDirection'][k] for k in 'xyz']
            norm=math.sqrt(sum(v*v for v in n));n=[v/norm for v in n]
            dot=sum(p*q for p,q in zip(n,x));x=[p-q*dot for p,q in zip(x,n)]
            norm=math.sqrt(sum(v*v for v in x));assert norm>1e-12
            segment['normal']=dict(zip('xyz',n));segment['xDirection']=dict(zip('xyz',(v/norm for v in x)))
    return definition


def identity(source,saved):
    assert saved['documentId']==source['documentId'] and saved['metadata']['title']==source['metadata']['title']
    source_entities={e['id'] for e in source['entities']}
    origins={e['id'] for e in saved['entities'] if e['id'] not in source_entities}
    group_ids={g['id'] for g in source['groups']}
    for key in ['entities','features','groups']:
        original={x['id']:x for x in source[key]};restored={x['id']:x for x in saved[key]}
        assert original.keys()<=restored.keys()
        extra=[x for k,x in restored.items() if k not in original]
        if key=='entities':
            assert len(extra)==3 and {x['displayName'] for x in extra}=={'top_XY','front_XZ','side_YZ'}
            assert all(x['kind']=='work_plane' for x in extra)
            assert all(x.get('partProperties')==restored[k].get('partProperties') for k,x in original.items())
            assert all(x[field]==restored[k][field] for k,x in original.items()
                       for field in ['kind','displayName','groupId','visibility'])
        elif key=='features':
            assert len(extra)==3 and all(x['type']=='create_work_plane' and x['definition']['isOriginPlane'] for x in extra)
            assert all(x['type']==restored[k]['type'] and same_defined_values(canonical_definition(x),restored[k]['definition']) for k,x in original.items())
        else:
            assert not extra
            for k,x in original.items():
                expected=copy.deepcopy(x);actual=copy.deepcopy(restored[k])
                # The loader rebuilds group children before entity children and
                # appends the three default origin planes; preserve each order.
                order=x['childOrder'];expected['childOrder']=[i for i in order if i in group_ids]+[i for i in order if i not in group_ids]
                actual['childOrder']=[i for i in actual['childOrder'] if i not in origins]
                assert same_defined_values(expected,actual)


def contacts(design, report):
    expected=design.get('attachmentChecks',[]);actual=report.get('attachmentChecks',[])
    assert expected and len(expected)==len(actual), 'missing attachment checks'
    for source,result in zip(expected,actual):
        assert all(result.get(k)==v for k,v in source.items()), 'stale attachment check'
        assert result['passed'] and 0<=result['distanceMm']<=source['maximumDistanceMm']
        assert result['contactAreaMm2']>=source['minimumAreaMm2']>0


def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);ap.add_argument('--run',default='04')
    a=ap.parse_args();w=a.workspace;run=a.run;result=dict(timeUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),models=[])
    for split in [False,True]:
        prefix='split-' if split else '';suffix='-split' if split else ''
        source=w/('candidate'+run)/('kuha115-ho80-revised'+suffix+'.kcd2')
        design=load(w/('candidate'+run)/('design'+suffix+'.json'))
        check=w/(prefix+'native'+run);export=w/(prefix+'exports'+run)
        validation=load(check/'validation.json');r=load(export/'print-parts.json');mesh=load(export/'stl-validation.json')
        all_design=load(w/('candidate'+run)/('design'+suffix+'-all-pairs.json'))
        all_report=load(w/(prefix+'all-pairs'+run)/'print-parts.json')
        assert validation['passed'] and validation['unchanged'] and validation['modelSha']==sha(source)
        assert r['preparedToken']==all_report['preparedToken']==validation['token']
        expected={e['id'] for e in design['entities'] if e['kind']=='part' and e['visibility']=='visible'}
        assert len(expected)>190 and expected=={p['id'] for p in r['parts']}=={p['id'] for p in all_report['parts']}
        assert len(expected)==r['visibleParts']==mesh['parts'] and not r['failures'] and not mesh['failures'] and not all_report['failures']
        assert not r['checkOnly'] and all_report['checkOnly']
        assert len(r['assemblyChecks'])==len(design['assemblyChecks'])>140
        refined=design.get('detailRevision')=='2026-10-25-shoulder-refinement'
        expected_points=476 if design.get('bogieComponent') else 412 if refined else 314 if design.get('detailRevision') else 227
        if design.get('contours'):
            from contour_details import REVISION
            assert design['contours']['revision']==REVISION
            assert design['contours']['addedPointChecks']==473
            assert len(design['contours']['doorSections'])==14
            expected_points+=473
        assert len(r['pointChecks'])==len(design['pointChecks'])==expected_points
        assert len(r['splitChecks'])==(6 if split else 0)
        assert all(c['passed'] for k in ['assemblyChecks','pointChecks','splitChecks'] for c in r[k])
        for key in ['assemblyChecks','pointChecks','splitChecks']:
            assert len(design.get(key,[]))==len(r[key])
            assert all(all(actual.get(k)==v for k,v in source.items()) for source,actual in zip(design.get(key,[]),r[key])), 'stale geometry probes'
        if design.get('contours'):
            assert all(c['entity'] in expected for c in design['contours']['doorSections'])
            assert len(design['keyParts']['integratedFloorApron'])==(2 if split else 1)
            assert set(design['keyParts']['integratedFloorApron'])<=expected
        if design.get('attachmentRevision'):
            contacts(design,r)
            assert not all_design['attachmentChecks'] and not all_report['attachmentChecks']
            assert all_design['broadPhase']['reusedChecks']['attachmentChecks']==len(r['attachmentChecks'])
        assert len(all_report['assemblyChecks'])==all_design['broadPhase']['candidates']>100
        assert all_design['broadPhase']['potentialPairs']==len(expected)*(len(expected)-1)//2
        if 'reusedChecks' in all_design['broadPhase']:
            receipt=all_design['broadPhase']['reusedChecks']
            expected_receipt=dict(reportSha256=sha(export/'print-parts.json'),preparedToken=validation['token'],
                pointChecks=len(r['pointChecks']),splitChecks=len(r['splitChecks']))
            if design.get('attachmentRevision'):expected_receipt['attachmentChecks']=len(r['attachmentChecks'])
            assert receipt==expected_receipt
            assert not all_report['pointChecks'] and not all_report['splitChecks']
        pairs=lambda checks:{frozenset((c['first'],c['second'])) for c in checks}
        assert pairs(all_report['assemblyChecks'])==pairs(all_design['assemblyChecks'])
        current_pairs={frozenset((p['id'],q['id'])) for p,q in itertools.combinations(r['parts'],2)
                       if all(min(p['bounds'][i+3],q['bounds'][i+3])-max(p['bounds'][i],q['bounds'][i])>=-1e-5 for i in range(3))}
        assert current_pairs==pairs(all_report['assemblyChecks'])
        assert all(c['passed'] and c['overlapMm3']<=1e-6 for c in all_report['assemblyChecks'])
        for p in r['parts']:
            assert p['valid'] and p['solids']==1 and p['stepRoundTrip'] and p['exported'] and p['volumeMm3']>0
            assert p['chordMm']==design['stlChordByEntity'].get(p['id'],design['stlChordMm'])
        doc=archive(source);saved_path=check/'cad-serialized.kcd2'
        token,saved=extract_roundtrip(check/'prepared.bin',saved_path);assert token==validation['token'];identity(doc,saved)
        if design.get('bogieComponent'):
            component=design['bogieComponent'];folder=source.parent
            provenance=load(folder/'tr62-component.json');native_proof=load(folder/'tr62-native-validation.json')
            native_parts=load(folder/'tr62-native-parts.json');library=archive(folder/'tr62-component.kcd2')
            assert sha(folder/'tr62-native.kcd2')==component['sourceSha256']==provenance['sourceSha256']==native_proof['modelSha']
            assert native_proof['passed'] and native_parts['preparedToken']==native_proof['token'] and not native_parts['failures']
            assert len(library['entities'])==len(library['features'])==8
            assert all(p['stepRoundTrip'] and p['solids']==1 for p in native_parts['parts'])
            assert sha(folder/'tr62-component.kcd2')==component['componentSha256']==provenance['componentSha256']
            source_parts={p['id']:p for p in native_parts['parts']};actual={p['id']:p for p in r['parts']}
            definitions={o['entityId']:f['definition'] for f in doc['features'] for o in f['outputs']}
            for old,new in component['frontEntities'].items():
                assert hashlib.sha256(definitions[new]['frozenBrep'].encode()).hexdigest()==provenance['shapeSha256'][old]
                original=source_parts[provenance['sourceEntities'][old]];placed=actual[new]
                assert abs(original['volumeMm3']-placed['volumeMm3'])<1e-6
                assert all(abs(original['bounds'][i]-placed['bounds'][i])<1e-6 for i in range(6))
            rear=[p for p in r['parts'] if '/10 台車' in p['groupPath'] and '/後台車' in p['groupPath']]
            assert len(rear)==7
            for part in rear:
                definition=definitions[part['id']];original=actual[definition['source']]
                assert definition['vector']==dict(x=172.5,y=0,z=0) and abs(part['volumeMm3']-original['volumeMm3'])<1e-6
                assert all(abs(part['bounds'][i]-original['bounds'][i]-(172.5 if i in [0,3] else 0))<1e-6 for i in range(6))
        assert design['scale']==80 and design['gaugeMm']==16.5
        gw=design['estimated']['gangwayWindow'];assert gw['width']==7.2 and gw['height']==5.8 and gw['width']>gw['height']
        rim=next(p for p in r['parts'] if p['name']=='横長貫通扉窓Hゴム')['bounds']
        assert abs(rim[4]-rim[1]-7.6)<1e-5 and abs(rim[5]-rim[2]-6.2)<1e-5
        if design.get('detailRevision'):
            assert design['estimated']['panoramicWindow']==dict(bottom=30.6,height=7.6)
            assert abs(design['estimated']['foreheadLongitudinalRadius']-(9.0 if refined else 5.875))<1e-10
            assert len(design['paintDecals'])==3
            checks=r['pointChecks']
            assert sum('額の中央と左右肩' in c['label'] for c in checks)==(81 if refined else 45)
            if refined:
                assert design['estimated']['roofNominalThickness']==1.0
                assert sum('法線方向0.85mm' in c['label'] for c in checks)==54
                assert sum('ジャンパ栓の背後' in c['label'] for c in checks)==4
                if design.get('contours'):
                    assert sum('裾絞り扉' in c['label'] for c in checks)==434
                    assert sum('キセ下縁の連続' in c['label'] or 'キセ下面の屋根' in c['label'] for c in checks)==30
                    assert sum('床板とスカートの連続返し' in c['label'] for c in checks)==10
                    assert sum('スカート上部の開放維持' in c['label'] for c in checks)==3
                else:assert sum('前端台枠' in c['label'] for c in checks)==4
            assert sum('短縮した前面窓' in c['label'] for c in checks)==18
            assert sum('黒い軸箱外蓋' in c['label'] for c in checks)==16
            if design.get('bogieComponent'):
                assert design['bogieComponent']['rearTranslationMm']==172.5
                springs=design['bogieComponent']['springPathChecks']
                assert len(springs)==12 and max(s['maximumMm'] for s in springs)<.001
                assert sum('横向きシリンダの中央下側' in c['label'] for c in checks)==4
                assert sum('ディスクの両面を挟むパッド' in c['label'] for c in checks)==16
                assert sum('ディスク中央の放射状通気孔' in c['label'] for c in checks)==8
            assert sum('信号炎管の幅広い' in c['label'] for c in checks)==1
            assert sum('スカートの下絞り内側' in c['label'] for c in checks)==2
            assert sum('スカート下絞りの外側' in c['label'] for c in checks)==2
            expected_skirt=dict(topHalfWidth=17.1,bottomHalfWidth=16.0,bottomZ=6.6)
            if refined:expected_skirt['centreTopZ']=10.7
            assert design['estimated']['skirtOutline']==expected_skirt
            assert design['estimated']['flareCapRadius']==.78 and design['estimated']['frontGlazingInset']==.22
        maximum=max(max(p['bounds'][i+3]-p['bounds'][i] for i in range(3)) for p in r['parts'])
        if split:assert maximum<125
        else:
            body=next(p for p in r['parts'] if '/01 車体' in p['groupPath'])['bounds']
            assert abs(body[3]-body[0]-243.75)<.001 and abs(body[4]-body[1]-36.25)<.001
            assert abs(max(p['bounds'][5] for p in r['parts'] if '/02 屋根' in p['groupPath'])-45.675)<.001
            gutters=[p['bounds'] for p in r['parts'] if p['name'].endswith('雨樋')]
            assert len(gutters)==2 and abs(max(b[4] for b in gutters)-min(b[1] for b in gutters)-36.875)<.001
        result['models'].append(dict(name='split' if split else 'full',file=source.name,sourceSha256=sha(source),
            cadExeSha256=validation['exeSha'],nativeSeconds=validation['seconds'],parts=len(expected),
            features=len(doc['features']),groups=len(doc['groups']),assemblyChecks=len(r['assemblyChecks']),
            pointChecks=len(r['pointChecks']),splitReassemblyChecks=len(r['splitChecks']),
            attachmentChecks=len(r.get('attachmentChecks',[])),
            totalPartPairs=all_design['broadPhase']['potentialPairs'],brepInterferenceCandidates=len(all_report['assemblyChecks']),
            maximumPartExtentMm=maximum,closedBrep=True,stepRoundTrips=True,stlManifold=True,nativeArchiveRoundTripStructure=True))
    reopen=load(w/('reopen'+run)/'validation.json');geom=load(w/('reopen-geometry'+run)/'print-parts.json')
    original=load(w/('exports'+run)/'print-parts.json');saved=w/('native'+run)/'cad-serialized.kcd2'
    assert reopen['passed'] and reopen['modelSha']==sha(saved) and reopen['exeSha']==result['models'][0]['cadExeSha256']
    assert geom['preparedToken']==reopen['token'] and not geom['failures'] and geom['checkOnly']
    if load(w/('candidate'+run)/'design.json').get('attachmentRevision'):
        contacts(load(w/('candidate'+run)/'design.json'),geom)
    old={p['id']:p for p in original['parts']};new={p['id']:p for p in geom['parts']};assert old.keys()==new.keys()
    delta=max(abs(p['volumeMm3']-new[k]['volumeMm3']) for k,p in old.items());assert delta<1e-6
    assert all(abs(p['bounds'][i]-new[k]['bounds'][i])<1e-6 for k,p in old.items() for i in range(6))
    desktop=load(w/('desktop'+run)/'desktop.json')
    assert desktop['ready'] and desktop['sourceSha256']==reopen['modelSha'] and desktop['exeSha256']==reopen['exeSha']
    result.update(savedReopen=dict(passed=True,maximumVolumeDeltaMm3=delta,parts=len(old),seconds=reopen['seconds']),
                  desktopStartup=desktop,gangwayOpeningMm=[7.2,5.8],gangwayAspectRatio=7.2/5.8,passed=True)
    repo=Path(__file__).resolve().parents[3]
    result['sourceFilesSha256']={p:sha(repo/p) for p in ['tools/modeling/kuha115_ho/revised.py',
        'tools/modeling/kuha115_ho/build.py','tools/modeling/native_kcd.py','tools/modeling/kuha115_ho/fit_details.py',
        'tools/modeling/kuha115_ho/contour_details.py',
        'tools/modeling/kuha115_ho/review_revised.py','tools/modeling/kuha115_ho/assembly_candidates.py',
        'tools/modeling/series115_print/export_native.cpp','tools/modeling/series115_print/check_stl.py',
        'tools/modeling/kuha115_ho/render_revised.py','tools/modeling/series115_print/render_preview.py',
        'tools/modeling/kuha115_ho/detail_parts.py','tools/modeling/kuha115_ho/front_roof.py','tools/modeling/kuha115_ho/render_detailed.py',
        'tools/modeling/kuha115_ho/assemble_detailed.py','tools/modeling/kuha115_ho/bogie_details.py',
        'tools/modeling/kuha115_ho/bogie_library.py','tools/modeling/kuha115_ho/standalone_bogie.py',
        'tools/modeling/kuha115_ho/test_bogie_library.py','tools/modeling/kuha115_ho/desktop_model.py',
        'tools/modeling/kuha115_ho/render_contours.py','tools/modeling/kuha115_ho/test_contour_geometry.py',
        'tools/modeling/kuha115_ho/assemble_contoured.py']}
    (w/'review-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    print(json.dumps(result,ensure_ascii=True,indent=2))


if __name__=='__main__':main()
