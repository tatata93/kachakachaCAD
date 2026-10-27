"""Successive photo-reviewed design candidates; local radii are estimates."""
import math

REVISION='2026-10-27-ten-round-print-kit'


def enabled(model,stage=1):return getattr(model,'refinement_pass',0)>=stage


def longitudinal_radius(model):return 6.3 if enabled(model) else 9.0


def transverse_radius(model):return 48.0 if enabled(model,2) else 40.0


def side_pillars(model):
    if not enabled(model,6):return
    count=0
    for windows in model.layout.values():
        for w in windows:
            if any(abs(w['x']-x)<1e-6 for x in [113.75,188.75]):w['x']-=.8;count+=1
    assert count==4,'Expected four previously near-zero-width window/door pillars'


def pillar_checks(model,body):
    if not enabled(model,6):return
    for sign in [-1,1]:
        for x in [117.7,192.7]:
            for z in [27.,30.,34.]:
                model.points.extend([dict(entity=body,label='窓と客扉の間に0.825mmの柱を残す',point=[x,sign*17.6,z],expectedInside=True),
                    dict(entity=body,label='柱の隣の客窓を埋めない',point=[x-.8,sign*17.6,z],expectedInside=False)])


def replace_part(model,key,new):
    from fit_details import replace_checks
    old=model.finished[key];replace_checks(model,old,new);model.finished[key]=new
    if old in model.mesh_chords:model.mesh_chords[new]=model.mesh_chords[old]
    for name,parts in model.keyparts.items():model.keyparts[name]=[new if p==old else p for p in parts]


def roof_keys(model):
    if not enabled(model,8):return
    from fit_details import contact
    roof=model.finished['roof'];group=model.entities[roof]['groupId'];keys=[]
    for sign in [-1,1]:
        for x in [72.,90.,146.,175.,220.]:
            keys.extend([model.box('屋根の内側位置決め脚',x,min(sign*16.125,sign*16.925),37.5,4.,.8,1.85,group),
                model.box('屋根へ一体化する位置決め脚の肩',x,min(sign*16.125,sign*17.85),39.15,4.,1.725,.8,group)])
    joined=model.joined('中空屋根・内側位置決め脚一体',[roof]+keys,group,'屋根グレー',
        note='模型用位置決め。側壁内面との設計隙間0.2mm。実樹脂でのはめ合い確認は未実施。')
    replace_part(model,'roof',joined);contact(model,joined,model.finished['body'],'屋根下縁と車体上縁の接着',20.)
    for sign in [-1,1]:
        for x in [72.,90.,146.,175.,220.]:
            model.points.append(dict(entity=joined,label='屋根の位置決め脚の肉厚中心',point=[x+2,sign*16.525,38.2],expectedInside=True))


def floor_receivers(model):
    if not enabled(model,9):return
    from fit_details import contact
    body=model.finished['body'];group=model.entities[body]['groupId'];pads=[]
    for sign in [-1,1]:
        for x in [72.,148.,216.]:pads.append(model.box('床板上面を受ける車体内側の座',x,min(sign*15.65,sign*16.85),14.95,6.,1.2,1.05,group))
    joined=model.joined('車体・床板を受ける内側接着座一体',[body]+pads,group)
    replace_part(model,'body',joined)
    for sign in [-1,1]:
        for x in [72.,148.,216.]:
            contact(model,joined,model.finished['floor'],'床板上面と車体受け座の端面',1.,[x-.01, min(sign*15.64,sign*16.86),14.8,x+6.01,max(sign*15.64,sign*16.86),15.1])
            model.points.extend([dict(entity=joined,label='既存壁から突出する床板受け座の肉厚',point=[x+3,sign*15.8,15.5],expectedInside=True),
                dict(entity=joined,label='床板受け座の下方の空間',point=[x+3,sign*15.8,14.8],expectedInside=False)])


def interior_supports(model):
    if not enabled(model,10):return
    from fit_details import contact
    tray=model.finished['interior'];group=model.entities[tray]['groupId'];pads=[]
    for sign in [-1,1]:
        for x in [12.,90.,160.,225.]:pads.append(model.box('室内トレー下面の連続支持座',x,min(sign*10.,sign*12.),14.95,6.,2.,.25,group))
    joined=model.joined('室内トレー・床板に接する支持座一体',[tray]+pads,group,'室内クリーム')
    replace_part(model,'interior',joined)
    contact(model,joined,model.finished['floor'],'室内トレーの8支持座と床板',60.)
    for sign in [-1,1]:
        for x in [12.,90.,160.,225.]:
            contact(model,joined,model.finished['floor'],'室内支持座ごとの床板との端面',10.,[x-.01,min(sign*9.99,sign*12.01),14.8,x+6.01,max(sign*9.99,sign*12.01),15.1])
            model.points.append(dict(entity=joined,label='室内トレーの支持座中心',point=[x+3,sign*11.,15.075],expectedInside=True))


def mounting_features(model):
    roof_keys(model);floor_receivers(model);interior_supports(model)


def plan_front_x(y,inset=0):
    y=abs(y);half=5.3;r=3.8-inset;side=18.125-inset;angle=math.radians(8)
    cy=side-r;yt=cy+r*math.sin(angle)
    cx=inset+r*math.cos(angle)+math.tan(angle)*(yt-half)
    if y<=half:return inset
    if y<=yt:return inset+math.tan(angle)*(y-half)
    return cx-math.sqrt(max(0,r*r-(y-cy)**2))


def roof_front_x(spec,y,z,inset=0):
    radius=spec.radius-inset;top=spec.height(y,inset);dz=z-(top-radius)
    if z>=top:return None
    circular=inset if dz<=0 else spec.radius-math.sqrt(max(0,radius*radius-dz*dz))
    return max(plan_front_x(y,inset),circular)


def forehead_checks(model,spec,roof):
    # These are classified against the evaluated native BRep. The separate STL
    # section audit compares the actual roof/body boundaries across revisions.
    for y in [-16.5,-14,-10,-5.3,0,5.3,10,14,16.5]:
        for z in [39.04,39.5,40.,40.5]:
            outer=roof_front_x(spec,y,z)
            if outer is None:continue
            inner=roof_front_x(spec,y,z,1.)
            middle=(outer+inner)/2 if inner is not None and inner>outer+.02 else outer+.12
            for x,inside in [(outer-.08,False),(middle,True)]:
                model.points.append(dict(entity=roof,label='改訂額・平面クリップ後の外側と肉厚中心',point=[x,y,z],expectedInside=inside))
            if inner is not None and inner>outer+.02:
                model.points.append(dict(entity=roof,label='改訂額・平面クリップ後の内側空間',point=[inner+.08,y,z],expectedInside=False))
        # The old long radius leaves a visible ledge at the body/roof joint.
        model.points.append(dict(entity=roof,label='車体前面の直上に連続する屋根下縁',
            point=[plan_front_x(y)+.08,y,39.04],expectedInside=True))
        if enabled(model,2):
            sine=.9;cosine=math.sqrt(1-sine*sine)
            h=spec.height(y);derivative=(spec.height(y+.0001)-spec.height(y-.0001))/.0002
            normal=(-cosine,-sine*derivative,sine)
            length=math.sqrt(sum(v*v for v in normal));normal=tuple(v/length for v in normal)
            anchor=(spec.radius*(1-cosine),y,h-spec.radius+spec.radius*sine)
            for distance,inside in [(.12,False),(-.85,True)]:
                model.points.append(dict(entity=roof,label='改訂額・法線方向の外側と残存肉厚',
                    point=[p+distance*n for p,n in zip(anchor,normal)],expectedInside=inside))


def manifest(model):
    stage=model.refinement_pass
    return dict(revision=REVISION,stage=stage,complete=False,
        longitudinalRadiusMm=longitudinal_radius(model),transverseRadiusMm=transverse_radius(model),
        dimensionStatus='Local radii are photo-reviewed model design estimates, not measured prototype dimensions.',
        changes=['Forehead/body boundary follows the folded plan without the old exterior step']+
            (['Shallower transverse crown with a larger tangent shoulder radius'] if stage>=2 else [])+
            (['Destination film placed 0.07 mm behind its housing lip, with finite-contact probes'] if stage>=3 else [])+
            (['Lamp barrels extend to and are trimmed by the actual folded outer body'] if stage>=4 else [])+
            (['Optical rings, reflector seats and lenses have explicit mating surfaces'] if stage>=5 else [])+
            (['Four window/door pillars increased from 0.025 to 0.825 mm by moving fixed windows 0.8 mm'] if stage>=6 else [])+
            (['Roof walkways retain their full 0.3 mm board thickness above the new roof, with four fitted supports'] if stage>=7 else [])+
            (['Roof has internal alignment legs with a nominal 0.2 mm side clearance'] if stage>=8 else [])+
            (['Body has six internal floor receivers, clear of the glazing and interior tray'] if stage>=9 else [])+
            (['Interior tray has eight pads seated on the floor instead of a 0.2 mm floating gap'] if stage>=10 else []),
        lampStacks=getattr(model,'lamp_stacks',[]),finishedParts=model.finished)
