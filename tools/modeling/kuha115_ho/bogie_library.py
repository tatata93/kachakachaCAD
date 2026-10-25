"""Reuse CAD-validated native bogie solids as independent assembly components.

The separate tr62-native.kcd2 retains the editable sweep/Boolean source. The
assembly stores exact BReps, not a triangulated or re-fitted approximation.
"""
from pathlib import Path
import argparse,copy,hashlib,io,json,struct,zipfile,sys,shutil
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from native_kcd import Model,vec


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_parts(prepared):
    stream=io.BytesIO(Path(prepared).read_bytes())
    def number():return struct.unpack('>Q',stream.read(8))[0]
    def field():
        size=number();assert size<512*1024*1024
        data=stream.read(size);assert len(data)==size;return data
    assert field()==b'kachakacha-prepared-document' and number()==2
    token=field().decode();archive=field();parts={}
    for _ in range(number()):
        key=field().decode();parts[key]=field().decode()
    return token,archive,parts


def package(source,prepared,validation,report,source_design,output):
    v=json.loads(Path(validation).read_text(encoding='utf-8'));r=json.loads(Path(report).read_text(encoding='utf-8'))
    design=json.loads(Path(source_design).read_text(encoding='utf-8'))
    token,archive,shapes=read_parts(prepared)
    assert v['passed'] and v['unchanged'] and sha(source)==v['modelSha']
    assert r['preparedToken']==token==v['token'] and not r['failures'] and r['visibleParts']==8
    assert all(p['valid'] and p['solids']==1 for p in r['parts'])
    assert all(c['passed'] for k in ['assemblyChecks','pointChecks'] for c in r[k])
    assert len(r['pointChecks'])==len(design['pointChecks'])==40
    assert len(design['springChecks'])==12 and max(c['maximumMm'] for c in design['springChecks'])<.001
    with zipfile.ZipFile(io.BytesIO(archive)) as z:doc=json.loads(z.read('document.json'))
    from review_revised import archive as source_archive,identity
    identity(source_archive(Path(source)),doc)
    entities={e['id']:e for e in doc['entities']};expected={p['id'] for p in r['parts']}
    m=Model('TR62系台車｜検査済み独立部品 1/80 HO16.5');g=m.group('台車単独｜詳細原本はtr62-native.kcd2')
    mapping={};colors={};roles={};shape_hashes={}
    for p in r['parts']:
        original=entities[p['id']];name=original['displayName'];brep=shapes[p['id']]
        new=m.add(name,'part','freeze_derived',dict(sources=[],frozenBrep=brep),g)
        m.entities[new]['editPolicy']='frozen';m.entities[new]['partProperties']=copy.deepcopy(original['partProperties'])
        mapping[p['id']]=new;colors[new]='金属銀' if '輪軸・内側' in name else '黒'
        role='frame' if '心皿穴を貫通' in name else 'wheelset' if '輪軸・内側' in name else 'keeper' if '局所輪軸押え' in name else 'plow'
        roles.setdefault(role,[]).append(new);shape_hashes[new]=hashlib.sha256(brep.encode()).hexdigest()
    assert {key:len(value) for key,value in roles.items()}==dict(frame=1,wheelset=2,keeper=4,plow=1)
    points=[dict(c,entity=mapping[c['entity']]) for c in r['pointChecks']]
    checks=[dict(c,first=mapping[c['first']],second=mapping[c['second']]) for c in r['assemblyChecks']
            if c['first'] in expected and c['second'] in expected]
    output=Path(output);output.mkdir(exist_ok=False,parents=True);m.save(output/'tr62-component.kcd2')
    provenance=dict(version=1,sourceSha256=sha(source),preparedSha256=sha(prepared),evaluatorSha256=v['exeSha'],
        validationToken=token,componentSha256=sha(output/'tr62-component.kcd2'),shapeSha256=shape_hashes,
        sourceEntities={new:old for old,new in mapping.items()},roles=roles,colors=colors,pointChecks=points,assemblyChecks=checks,
        springPathChecks=design['springChecks'],
        note='Exact CAD BReps. Native detailed source is shipped separately. Rear bogie uses a rigid translation of common parts, without front plow.')
    (output/'tr62-component.json').write_text(json.dumps(provenance,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    (output/'tr62-native.kcd2').write_bytes(Path(source).read_bytes())
    shutil.copy2(validation,output/'tr62-native-validation.json')
    shutil.copy2(report,output/'tr62-native-parts.json')
    shutil.copy2(source_design,output/'tr62-design.json')
    print(json.dumps(dict(parts=8,componentBytes=(output/'tr62-component.kcd2').stat().st_size,sourceSha256=sha(source))))


def place(m,root,component):
    component=Path(component);p=json.loads(component.with_suffix('.json').read_text(encoding='utf-8'))
    assert p['version']==1 and sha(component)==p['componentSha256']
    with zipfile.ZipFile(component) as z:d=json.loads(z.read('document.json'))
    entities={e['id']:e for e in d['entities']};features={f['id']:f for f in d['features']}
    g0=m.group('10 台車｜TR62系構造・HO16.5機構',root)
    m.keyparts['bogies']=[];m.keyparts['wheelsets']=[];front={}
    for rear in [False,True]:
        g=m.group('後台車' if rear else '前台車',g0);mapping={};dx=172.5 if rear else 0
        for role in ['frame','wheelset','keeper','plow']:
            if rear and role=='plow':continue
            for old in p['roles'][role]:
                e=entities[old];definition=features[e['createdBy']]['definition'];name=e['displayName']
                if rear:
                    name=name.replace('前','後',1)
                    new=m.add(name,'part','transform_part',dict(method=1,source=front[old],vector=vec((dx,0,0)),point=vec((0,0,0)),angleRad=0),g,[front[old]])
                else:
                    assert hashlib.sha256(definition['frozenBrep'].encode()).hexdigest()==p['shapeSha256'][old]
                    new=m.add(name,'part','freeze_derived',copy.deepcopy(definition),g);m.entities[new]['editPolicy']='frozen';front[old]=new
                m.entities[new]['partProperties']=copy.deepcopy(e['partProperties']);m.colors[new]=p['colors'][old];mapping[old]=new
                if role=='frame':m.keyparts['bogies'].append(new)
                if role=='wheelset':m.keyparts['wheelsets'].append(new)
        for c in p['pointChecks']:
            m.points.append(dict(c,entity=mapping[c['entity']],point=[c['point'][0]+dx,*c['point'][1:]]))
        for c in p['assemblyChecks']:
            if c['first'] in mapping and c['second'] in mapping:
                m.checks.append(dict(c,first=mapping[c['first']],second=mapping[c['second']]))
        m.clear(mapping[p['roles']['frame'][0]],m.finished['floor'],('後' if rear else '前')+'台車と床板')
    m.bogie_component=dict(sourceArchive='tr62-native.kcd2',component='tr62-component.kcd2',
        sourceSha256=p['sourceSha256'],componentSha256=p['componentSha256'],evaluatorSha256=p['evaluatorSha256'],
        shapeSha256=p['shapeSha256'],frontEntities=front,rearTranslationMm=172.5,springPathChecks=p['springPathChecks'])


if __name__=='__main__':
    a=argparse.ArgumentParser();a.add_argument('source');a.add_argument('prepared');a.add_argument('validation');a.add_argument('report');a.add_argument('source_design');a.add_argument('output')
    p=a.parse_args();package(p.source,p.prepared,p.validation,p.report,p.source_design,p.output)
