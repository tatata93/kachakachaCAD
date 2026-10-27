"""Manufacturing panels derived from the validated native design, never a mesh."""
from pathlib import Path
import argparse,copy,hashlib,itertools,json,math,uuid,zipfile
from revised import Revised
from fit_details import contact

NS=uuid.UUID('96dfe3d6-e07d-49ab-af6d-115122208027')


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):return json.loads(path.read_text(encoding='utf-8'))


def restore(source,design,compact):
    with zipfile.ZipFile(source) as archive:doc=json.loads(archive.read('document.json'))
    m=Revised(True);m.namespace=uuid.uuid5(NS,'compact' if compact else 'panels');m.i=0;m.d=copy.deepcopy(doc)
    m.groups={g['id']:g for g in m.d['groups']};m.entities={e['id']:e for e in m.d['entities']};m.wires={}
    m.colors=copy.deepcopy(design['colors']);m.checks=copy.deepcopy(design['assemblyChecks']);m.points=copy.deepcopy(design['pointChecks'])
    m.attachment_checks=copy.deepcopy(design['attachmentChecks']);m.mesh_chords=copy.deepcopy(design['stlChordByEntity'])
    m.keyparts=copy.deepcopy(design['keyParts']);m.finished=copy.deepcopy(design['refinements']['finishedParts'])
    m.d['documentId']=str(uuid.uuid5(m.namespace,'native-panel-kit'));m.d['metadata']['title']='クハ115 長野色 1/80｜板組・小型造形版' if compact else 'クハ115 長野色 1/80｜板組版'
    for g in m.d['groups']:
        if g['parentGroupId'] is None:g['state']='hidden'
    for e in m.d['entities']:e['visibility']='hidden'
    return m


def body_bridges(m,g):
    """Hidden manufacturing webs; no tolerance-based fusion across a gap."""
    result=[]
    for sign in [-1,1]:
        for cx in [51.25,126.25,201.25]:
            for x in [cx-8.35,cx+7.4]:result.append(m.box('客扉を側板へ一体化する裏の連結座',x,min(sign*16.4,sign*17.1),18.,.95,.7,1.5,g))
        for x in [16.8,22.8]:result.append(m.box('乗務員扉を側板へ一体化する裏の連結座',x,min(sign*16.4,sign*17.1),18.,.95,.7,1.5,g))
        result.append(m.box('前面貫通扉の裏の連結座',.3,min(sign*4.45,sign*5.1),21.,.8,.65,1.0,g))
        result.append(m.box('後妻貫通扉の裏の連結座',243.2,min(sign*4.15,sign*4.65),21.,.4,.5,1.0,g))
    return result


def slice_box(m,source,name,g,bounds,chord=.006):
    x,y,z,xx,yy,zz=bounds;tool=m.box(name+'の切出し領域',x,y,z,xx-x,yy-y,zz-z,g)
    part=m.boolean(name,source,[tool],2,g,m.colors.get(source,'フォギーグレー'));m.mesh_chords[part]=chord;return part


def split_x(m,source,name,g,x):
    chord=m.mesh_chords.get(source,.006)
    parts=[slice_box(m,source,name+' 前',g,[-5,-22,-1,x,22,52],chord),slice_box(m,source,name+' 後',g,[x,-22,-1,250,22,52],chord)]
    return parts


def glazing_strips(m,g,parts):
    films=[]
    for sign in [-1,1]:
        panes=[p for p in parts if '/04 透明板' in p['groupPath'] and (p['bounds'][1]+p['bounds'][4])*sign>0]
        assert len(panes)>15
        low=min(p['bounds'][0] for p in panes);high=max(p['bounds'][3] for p in panes)
        additions=[m.box('側面透明材を一枚にする裏の連続縁',low,min(sign*16.7,sign*17.0),24.0,high-low,.3,.6,g)]
        for p in panes:
            left,right=p['bounds'][0],p['bounds'][3];bottom=p['bounds'][2]
            additions.append(m.box('透明窓と連続縁の内側接続',(left+right)/2-.4,min(sign*16.7,sign*17.0),24.4,.8,.3,max(.2,bottom+.3-24.4),g))
        film=m.joined('側面の窓をまとめた透明材 '+str(sign),[p['id'] for p in panes]+additions,g,'透明')
        m.mesh_chords[film]=.002;films.append(film)
    return films


def compact_backers(m,root,masters,panels):
    g=m.group('05 分割部の接着用添え板',root);backs=[]
    floor=m.box('床板の継ぎ目を裏から支える添え板',115.875,-4,12.9,12,8,.75,g)
    m.finish(floor,'床下グレー')
    roof=m.box('屋根の継ぎ目の内側添え板素材',116.875,-4,44.,10,8,1.,g)
    roof=m.boolean('屋根内面へ沿う接着添え板',roof,[masters['roof']],1,g,'屋根グレー');m.mesh_chords[roof]=.002
    tray=m.box('室内トレーの継ぎ目の添え板',116.875,-4,15.55,10,8,.6,g)
    m.finish(tray,'室内クリーム')
    backs.extend([floor,roof,tray])
    for key,part,minimum in [('floor',floor,30.),('roof',roof,20.),('interior',tray,25.)]:
        for suffix in ['Front','Rear']:contact(m,part,panels[key+suffix],key+'継ぎ目の前後の接着面',minimum)
    for sign,key in [(-1,'driver'),(1,'assistant')]:
        strip=m.box('側板の継ぎ目の内側添え板素材',113.8,min(sign*16.35,sign*17.35),21.5,8.,1.,1.2,g)
        strip=m.boolean('裾絞りと扉の裏に合わせた側板添え板',strip,[masters['body']],1,g)
        m.mesh_chords[strip]=.002;backs.append(strip)
        for suffix in ['Front','Rear']:contact(m,strip,panels[key+suffix],'側板継ぎ目の前後の接着面',1.)
    return backs


def build(source,design,report,output,compact=False):
    m=restore(source,design,compact);root=m.group('板組キット｜印刷・仮組・接着');g=m.group('01 外板と製造用連結座',root)
    parts=report['parts'];opaque=[p for p in parts if p['manufacturing']['colorName'] not in ['透明','赤透明']]
    metal=[p for p in opaque if any(s in p['name'] for s in ['乗務員手すり','前面手掛けと取付脚','貫通扉の横手掛け','ワイパー','縦桟の両端','前頭屋根手掛け','取付脚の背面を外板に合わせる','屋根曲面に合わせたループ手掛け'])]
    excluded={p['id'] for p in metal};couplers=[p for p in opaque if '密着連結器・取付座' in p['name']]
    excluded.update(p['id'] for p in couplers)
    categories={key:[] for key in ['body','roof','floor','interior','bogie','cooler']}
    climate=set(design['keyParts']['climateUnit']+design['keyParts']['climateFans'])
    for p in opaque:
        if p['id'] in excluded:continue
        path=p['groupPath'];key='cooler' if p['id'] in climate else 'roof' if '/02 屋根' in path or '/07 屋根' in path else 'floor' if '/08 床板' in path or '/09 床下' in path else 'interior' if '/11 室内' in path else 'bogie' if '/10 台車' in path else 'body'
        categories[key].append(p['id'])
    masters={}
    doors={s['entity'] for s in design['contours']['doorSections']}|{design['keyParts']['gangway'][0]}
    doors.update(p['id'] for p in parts if p['name']=='後妻貫通扉')
    structural=[m.finished['body']]+[p for p in categories['body'] if p in doors]
    structural=m.joined('外板・裾絞り扉・裏連結座の構造原形',[structural[0]]+body_bridges(m,g)+structural[1:],g)
    frames=[p['id'] for p in parts if p['id'] in categories['body'] and ('Hゴム' in p['name'] or '窓枠' in p['name'])]
    masters['body']=m.joined('外板・窓枠・扉の製造用一体原形',[structural]+frames,g)
    separate_body=[p for p in categories['body'] if p not in doors and p not in frames and p!=m.finished['body']]
    masters['roof']=m.joined('屋根・表示器・通風器・支持座一体原形',categories['roof'],g,'屋根グレー')
    masters['floor']=m.joined('床板・スカート・床下箱一体原形',categories['floor'],g,'床下グレー')
    masters['interior']=categories['interior'][0];assert len(categories['interior'])==1
    cooler=m.joined('AU75G・ファン一体',categories['cooler'],g,'アルミ銀');m.mesh_chords[cooler]=.002
    for key in ['body','roof']:m.mesh_chords[masters[key]]=.006
    m.mesh_chords[masters['floor']]=.01
    contact(m,masters['roof'],masters['body'],'板組版の屋根と外板の接着面',20.)
    contact(m,masters['roof'],cooler,'板組版の冷房キセと屋根の接着面',6.)
    for sign in [-1,1]:
        for x in [72.,148.,216.]:contact(m,masters['body'],masters['floor'],'板組版の床板受け座',1.,
            [x-.01,min(sign*15.64,sign*16.86),14.8,x+6.01,max(sign*15.64,sign*16.86),15.1])
    split_checks=[];body=masters['body'];panels={
        'front':slice_box(m,body,'前妻｜パノラミック窓の回込まで一体',g,[-5,-22,-1,9.2,22,52]),
        'rear':slice_box(m,body,'後妻',g,[242.75,-22,-1,250,22,52]),
        'driver':slice_box(m,body,'運転士側の側板',g,[9.2,-22,-1,242.75,0,52]),
        'assistant':slice_box(m,body,'助手側の側板',g,[9.2,0,-1,242.75,22,52])}
    split_checks.append(dict(source=body,parts=list(panels.values()),label='四枚の外板を再結合して製造原形へ一致'))
    backs=[]
    if compact:
        for key in ['driver','assistant']:
            old=panels.pop(key);cut=split_x(m,old,key,g,117.8);panels[key+'Front'],panels[key+'Rear']=cut
            split_checks.append(dict(source=old,parts=cut,label=key+'・窓と扉の間で分割'))
        for key in ['roof','floor','interior']:
            cut=split_x(m,masters[key],key,g,121.875)
            panels[key+'Front'],panels[key+'Rear']=cut;split_checks.append(dict(source=masters[key],parts=cut,label=key+'の前後分割'))
    else:panels.update({key:masters[key] for key in ['roof','floor','interior']})
    if compact:backs=compact_backers(m,root,masters,panels)
    glasses=[p for p in parts if p['manufacturing']['colorName'] in ['透明','赤透明']]
    glass_group=m.group('02 透明材・切り出し形状',root)
    films=glazing_strips(m,glass_group,glasses)
    for film in films:contact(m,film,masters['body'],'板組版の側面透明材と外板の接着面',2.)
    if compact:
        film_halves=[]
        for film in films:
            cut=split_x(m,film,'側面透明材',glass_group,117.8);film_halves.extend(cut)
            split_checks.append(dict(source=film,parts=cut,label='透明材を窓の間で前後分割'))
        films=film_halves
    retained_glass=[p['id'] for p in glasses if '/04 透明板' not in p['groupPath']]
    def show(ident,group):
        old=m.entities[ident]['groupId']
        if old!=group:
            m.groups[old]['childOrder'].remove(ident);m.groups[group]['childOrder'].append(ident);m.entities[ident]['groupId']=group
        m.entities[ident]['visibility']='visible'
    bogie_group=m.group('03 台車・輪軸・押え',root);coupler_group=m.group('04 着脱する連結器',root)
    metal_group=m.group('06 金属線・薄板の参照（必要時だけ表示）',root)
    for p in metal:
        show(p['id'],metal_group);m.entities[p['id']]['visibility']='hidden'
    for ident in categories['bogie']:show(ident,bogie_group)
    for ident in retained_glass:show(ident,glass_group)
    for p in couplers:show(p['id'],coupler_group)
    fitting_group=m.group('07 塗装後に接着する別部品',root)
    for ident in separate_body:show(ident,fitting_group)
    for ident in panels.values():show(ident,g)
    for ident in backs:m.entities[ident]['visibility']='visible'
    m.entities[cooler]['visibility']='visible'
    for ident in films:m.entities[ident]['visibility']='visible'
    # Manufacturing reference solids remain editable but hidden after slicing.
    for ident in masters.values():
        if ident not in panels.values():m.entities[ident]['visibility']='hidden'
    m.d['activeGroupId']=root;m.d['uiState']={'activeMode':'part','theme':'windows95'}
    m.d['metadata']['description']='Native panel manufacturing derivative; nominal panel reconnection and continuous insertion are checked separately. Physical printing and running not tested.'
    d=copy.deepcopy(design);d.update(groups=m.d['groups'],entities=m.d['entities'],colors=m.colors,stlChordByEntity=m.mesh_chords,
        assemblyChecks=m.checks,pointChecks=m.points,attachmentChecks=m.attachment_checks,splitChecks=split_checks,
        insertionChecks=[dict(label='屋根を上へ外す経路の逆順で組立',moving=[masters['roof']]+([backs[1]] if compact else []),fixed=[masters['body']]+films+(backs[3:] if compact else []),vectorMm=[0,0,20]),
            dict(label='床板と室内を下へ外す経路の逆順で組立',moving=[masters['floor'],masters['interior']]+([backs[0],backs[2]] if compact else []),fixed=[masters['body']]+films+(backs[3:] if compact else []),vectorMm=[0,0,-45])],
        kit=dict(revision='2026-10-27-panels',compact=compact,complete=False,sourceModelSha256=sha(source),panels=panels,masters=masters,backers=backs,
            climateUnit=cooler,sideGlazing=films,metalTemplates=[dict(id=p['id'],name=p['name']) for p in metal],
            nominalSplitX=dict(front=9.2,rear=242.75,side=117.8 if compact else None,longitudinal=121.875 if compact else None),
            manufacturingAdditions='Hidden webs connect the door leaves to the main shell; window frames are fused in a second step. Fine fittings remain separate, and specified metal details are reference templates.',
            physicalPrintingTested=False,runningTested=False))
    output.mkdir(parents=True,exist_ok=False);m.save(output/'kuha115-panel-kit.kcd2')
    (output/'design.json').write_text(json.dumps(d,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    print(json.dumps(dict(parts=sum(e['kind']=='part' and e['visibility']=='visible' for e in m.d['entities']),splitChecks=len(split_checks),compact=compact)))


def main():
    ap=argparse.ArgumentParser();ap.add_argument('source',type=Path);ap.add_argument('design',type=Path);ap.add_argument('report',type=Path);ap.add_argument('native_proof',type=Path);ap.add_argument('output',type=Path);ap.add_argument('--compact',action='store_true')
    a=ap.parse_args();d=load(a.design);r=load(a.report);p=load(a.native_proof)
    assert d['refinements']['stage']==10 and not r['failures'] and p['passed'] and p['unchanged'] and p['modelSha']==sha(a.source) and p['token']==r['preparedToken']
    assert set(q['id'] for q in r['parts'])=={e['id'] for e in d['entities'] if e['kind']=='part' and e['visibility']=='visible'}
    build(a.source,d,r,a.output,a.compact)


if __name__=='__main__':main()
