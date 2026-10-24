"""Fail-closed checks for the new Kuha model and its actual CAD/export evidence."""
from pathlib import Path
import argparse, datetime, hashlib, io, json, math, struct, zipfile


def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()


def load(p):return json.loads(p.read_text(encoding='utf-8'))


def same_defined_values(source,saved):
    """The CAD adds explicit defaults and rounds binary arithmetic, but may not lose inputs."""
    if isinstance(source,dict):
        return isinstance(saved,dict) and all(k in saved and same_defined_values(v,saved[k]) for k,v in source.items())
    if isinstance(source,list):
        return isinstance(saved,list) and len(source)==len(saved) and all(same_defined_values(x,y) for x,y in zip(source,saved))
    if isinstance(source,(float,int)) and not isinstance(source,bool):
        return isinstance(saved,(float,int)) and math.isclose(source,saved,rel_tol=1e-12,abs_tol=1e-10)
    return source==saved


def extract_roundtrip(prepared, destination):
    stream=io.BytesIO(prepared.read_bytes())
    def field():
        size=struct.unpack('>Q',stream.read(8))[0]
        if size>512*1024*1024:raise ValueError('Oversized prepared field')
        data=stream.read(size)
        if len(data)!=size:raise ValueError('Truncated prepared field')
        return data
    assert field()==b'kachakacha-prepared-document'
    assert struct.unpack('>q',stream.read(8))[0]==2
    token=field().decode();archive=field()
    # This archive was written by the CAD's io::SaveDocument, not our generator.
    with zipfile.ZipFile(io.BytesIO(archive)) as z:document=json.loads(z.read('document.json'))
    destination.write_bytes(archive)
    return token,document


def main():
    ap=argparse.ArgumentParser();ap.add_argument('workspace',type=Path);args=ap.parse_args();w=args.workspace
    result=dict(timeUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),models=[])
    for name,stem,manifest,check,export in [
        ('full','kuha115-1000-ho80-new.kcd2','design.json','final-check','verified-main'),
        ('split','kuha115-1000-ho80-new-split.kcd2','design-split.json','split-check','verified-split')]:
        source=w/'delivery-model'/stem;design=load(w/'delivery-model'/manifest)
        validation=load(w/check/'validation.json');report=load(w/export/'print-parts.json');mesh=load(w/export/'stl-validation.json')
        with zipfile.ZipFile(source) as z:doc=json.loads(z.read('document.json'))
        assert validation['passed'] and validation['modelSha']==sha(source) and validation['unchanged']
        assert report['preparedToken']==validation['token']
        expected={e['id'] for e in design['entities'] if e['kind']=='part' and e['visibility']=='visible'}
        actual={e['id'] for e in report['parts']}
        assert expected and actual==expected and len(actual)==report['visibleParts']==mesh['parts']
        assert not report['failures'] and not mesh['failures']
        assert not report.get('checkOnly',False)
        assert len(report['assemblyChecks'])==len(design['assemblyChecks'])==75
        assert len(report['pointChecks'])==len(design['pointChecks'])==44
        assert all(x['passed'] for k in ['assemblyChecks','pointChecks','splitChecks'] for x in report[k])
        assert len(report['splitChecks'])==(6 if name=='split' else 0)
        for p in report['parts']:
            assert p['valid'] and p['solids']==1 and p['stepRoundTrip'] and p['exported']
            assert math.isfinite(p['volumeMm3']) and p['volumeMm3']>0
            assert p['chordMm']==design['stlChordByEntity'].get(p['id'],design['stlChordMm'])
        # Compare actual native BRep bounds to the adopted primary dimensions.
        if name=='full':
            body=next(p for p in report['parts'] if '01 車体' in p['groupPath'])['bounds']
            assert abs(body[3]-body[0]-243.75)<.001
            assert abs(body[4]-body[1]-36.875)<.001
            assert abs(max(p['bounds'][5] for p in report['parts'] if '02 屋根' in p['groupPath'])-45.675)<.001
        types={f['type'] for f in doc['features']}
        assert types<={'create_wire','extrude','create_solid','boolean','create_image'}
        assert design['scale']==80 and design['gaugeMm']==16.5
        token,saved=extract_roundtrip(w/check/'prepared.bin',w/check/'cad-serialized.kcd2')
        assert token==validation['token']
        for key in ['entities','features','groups']:
            source_ids={e['id'] for e in doc[key]};saved_ids={e['id'] for e in saved[key]}
            assert source_ids<=saved_ids
            extras=[e for e in saved[key] if e['id'] not in source_ids]
            if key=='entities':
                assert len(extras)==3 and {e['displayName'] for e in extras}=={'top_XY','front_XZ','side_YZ'}
                assert all(e['kind']=='work_plane' for e in extras)
            elif key=='features':
                assert len(extras)==3 and all(e['type']=='create_work_plane' and e['definition']['isOriginPlane'] for e in extras)
                by_id={e['id']:e for e in saved[key]}
                assert all(e['type']==by_id[e['id']]['type'] and same_defined_values(e['definition'],by_id[e['id']]['definition']) for e in doc[key])
            else:assert not extras
        assert saved['metadata']['title']==doc['metadata']['title']
        for e in doc['entities']:
            if e['kind']=='part':assert next(s for s in saved['entities'] if s['id']==e['id'])['partProperties']==e['partProperties']
        maximum=max(max(p['bounds'][i+3]-p['bounds'][i] for i in range(3)) for p in report['parts'])
        if name=='split':assert maximum<125
        result['models'].append(dict(name=name,file=stem,sourceSha256=sha(source),cadExeSha256=validation['exeSha'],
            nativeSeconds=validation['seconds'],parts=len(actual),assemblyChecks=len(report['assemblyChecks']),
            pointChecks=len(report['pointChecks']),splitReassemblyChecks=len(report['splitChecks']),
            maximumPartExtentMm=maximum,features=len(doc['features']),groups=len(doc['groups']),
            closedBrep=True,stepRoundTrips=True,stlManifold=True,nativeArchiveRoundTripStructure=True))
    result['passed']=True
    (w/'review-result.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    print(json.dumps(result,ensure_ascii=False,indent=2))


if __name__=='__main__':main()
