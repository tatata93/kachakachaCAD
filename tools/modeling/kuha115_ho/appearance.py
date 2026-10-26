"""Five incremental appearance revisions; local dimensions remain photo estimates."""
import math
from build import vec
from fit_details import contact

REVISION='2026-10-27-five-pass-appearance'


def enabled(m,step=1):return getattr(m,'appearance_pass',0)>=step


def skirt_setback(m):return 2.0 if enabled(m) else 0.


def shift_skirt(m,skirt,g):
    if not enabled(m):return skirt
    result=m.add('スカートを車体裾の後ろへ後退','part','transform_part',
        dict(method=1,source=skirt,vector=vec((skirt_setback(m),0,0)),point=vec((0,0,0)),angleRad=0),g,[skirt])
    m.entities[skirt]['visibility']='hidden'
    return m.finish(result,'床下グレー')


def front_x(y):return math.tan(math.radians(8))*(abs(y)-5.3)


def front_fittings(m,g,side):
    body=m.finished['body']
    for z,width in [(28.55,6.8),(21.4,1.7)]:
        cy=side*(10.7 if width>3 else 9.7)
        ends=[(front_x(y)-.25,y,z) for y in [cy-width/2,cy+width/2]]
        parts=[m.tube('折妻に沿う横手掛け',*ends,.13,g)]
        parts.extend(m.tube('手掛けの短い取付脚',p,(p[0]+.4,p[1],p[2]),.13,g) for p in ends)
        rail=m.joined('前面手掛けと取付脚',parts,g)
        rail=m.boolean('取付脚の背面を外板に合わせる',rail,[body],1,g,'アドバンスブルー' if z>26 else 'フォギーグレー')
        m.entities[body]['visibility']='visible'
        contact(m,rail,body,'前面手掛けと折妻外板',area=.05)
        m.points.append(dict(entity=rail,label='折妻に沿う手掛け中央',point=[front_x(cy)-.25,cy,z],expectedInside=True))
    lo,hi=side*12.3-2.7,side*12.3+2.7
    points=[(front_x(lo)-.65,lo,17),(front_x(hi)-.65,hi,17),
        (front_x(hi)+.8,hi,17),(front_x(lo)+.8,lo,17)]
    profile=m.wire('折妻に沿うステップ輪郭',[m.line(a,b) for a,b in zip(points,points[1:]+points[:1])],g)
    step=m.extrude('外板まで届く前面ステップ',[profile],(0,0,1),.2,g)
    step=m.boolean('ステップ背面を外板に合わせる',step,[body],1,g,'アルミ銀')
    m.entities[body]['visibility']='visible'
    contact(m,step,body,'前面ステップと折妻外板',area=.2)
    m.points.append(dict(entity=step,label='折妻に沿うステップ中央',point=[front_x(side*12.3)-.3,side*12.3,17.1],expectedInside=True))


def passenger_window(m):return (26.1,10.5) if enabled(m,3) else (27.7,8.9)


def passenger_handle(m,leaf,g,sign,x,half):
    from contour_details import shrink
    from fit_details import replace_checks
    hx=x+(6.3 if half<0 else 1.)
    face=lambda z:sign*(18.125-.28-shrink(z))
    cavities=[]
    for z,width,height in [(24.5,.33,1.1),(17.2,.42,.95)]:
        # Start beyond the outermost end of the tapered face. Starting at the
        # lower face leaves a thin membrane over most of the lower finger pull.
        p=m.rounded('半自動扉の指掛け凹部',(hx+(.65-width)/2,face(z+height)+sign*.02,z),
            (1,0,0),(0,0,1),width,height,min(width/2-.01,.15),g)
        cavities.append(m.extrude('指掛けの浅い凹み工具',[p],(0,-sign,0),.24,g))
    new=m.boolean('客扉の上下指掛けを彫る',leaf,cavities,1,g)
    replace_checks(m,leaf,new)
    next(row for row in m.contour_doors if row['entity']==leaf)['entity']=new
    o=m.rounded('縦長角形の手掛け外',(hx,face(25)+sign*.16,24.35),(1,0,0),(0,0,1),.65,1.4,.08,g)
    q=m.rounded('縦長角形の手掛け内',(hx+.16,face(25)+sign*.16,24.5),(1,0,0),(0,0,1),.33,1.1,.07,g)
    rim=m.finish(m.extrude('半自動扉の縦長手掛け枠',[o,q],(0,-sign,0),.16,g),'アルミ銀',.16)
    contact(m,rim,new,'客扉の縦長手掛けと扉板',area=.1)
    for z in [25.,17.65]:
        for depth,inside in [(.01,False),(.4,True)]:
            m.points.append(dict(entity=new,label='客扉指掛けの凹みと残る肉厚',point=[hx+.325,face(z)-sign*depth,z],expectedInside=inside))
    return new


def lower_side_windows(m):
    if not enabled(m,4):return
    count=0
    for sign,windows in m.layout.items():
        for w in windows:
            if w['type']=='toilet':continue
            w['z']-=2.4;count+=1
    assert count==23


def side_destination(m,g):
    if not enabled(m,4):return
    body=m.finished['body'];left=226.6;bottom=36.
    o=m.rounded('側面行先表示Hゴム外',(left,-18.325,bottom),(1,0,0),(0,0,1),7.8,2.6,.6,g)
    q=m.rounded('側面行先表示Hゴム内',(left+.2,-18.325,bottom+.2),(1,0,0),(0,0,1),7.4,2.2,.4,g)
    rim=m.finish(m.extrude('後部側面行先表示Hゴム',[o,q],(0,1,0),.2,g),'黒',.2)
    p=m.rounded('行先未指定の表示板',(left+.2,-18.225,bottom+.2),(1,0,0),(0,0,1),7.4,2.2,.4,g)
    board=m.finish(m.extrude('後部側面行先表示・空白幕',[p],(0,1,0),.1,g),'フォギーグレー',.1)
    m.keyparts['sideDestination']=[rim,board]
    contact(m,rim,body,'側面行先表示の枠と外板',area=.2)
    contact(m,board,body,'側面行先表示の板と外板',area=.2)
    contact(m,board,rim,'側面行先表示の板と枠',area=.1)
    for entity,point,inside in [(rim,[left+.1,-18.225,37.3],True),(rim,[230.5,-18.225,37.3],False),
        (board,[230.5,-18.175,37.3],True),(body,[230.5,-18.025,37.3],True)]:
        m.points.append(dict(entity=entity,label='側面行先表示の板と枠',point=point,expectedInside=inside))
    for sign,windows in m.layout.items():
        for w in windows:
            if w['type']=='toilet':continue
            for z,inside in [(37.1,True),(w['z']+.3,False)]:
                m.points.append(dict(entity=body,label='側窓の旧上端と新下端',point=[w['x'],sign*17.6,z],expectedInside=inside))


def jumper(m,g,y,skirt):
    dx=skirt_setback(m)
    p=m.rounded('ジャンパ栓箱輪郭',(dx-1.15,y-.65,11.35),(0,1,0),(0,0,1),1.3,1.6,.2,g)
    box=m.extrude('ジャンパ栓の角丸箱',[p],(1,0,0),.55,g)
    p=m.rounded('ジャンパ栓の前蓋',(dx-1.34,y-.55,11.45),(0,1,0),(0,0,1),1.1,1.35,.23,g)
    lid=m.extrude('ジャンパ栓の浅い蓋',[p],(1,0,0),.22,g)
    hinge=m.tube('蓋上縁のヒンジ',(dx-1.21,y-.43,12.85),(dx-1.21,y+.43,12.85),.09,g)
    points=[(dx-1.02,y,11.5),(dx-1.10,y,10.5),(dx-1.10,y+.7,9.85),(dx-.45,y+.7,9.85)]
    tangent=tuple(b-a for a,b in zip(points[0],points[1]));length=math.sqrt(sum(v*v for v in tangent))
    profile=m.circle('曲管の丸断面',points[0],tuple(v/length for v in tangent),.17,g)
    path=m.wire('一本の連続したジャンパ曲管',[dict(id=m.id(),type='cubic_bezier',controlPoints=[vec(p) for p in points])],g)
    pipe=m.add('ジャンパの短い連続曲管','part','create_solid',dict(method=2,profiles=[profile],axis=None,path=[path],
        angleRad=2*math.pi,symmetric=False,booleanMode=0,targets=[]),g,[profile,path])
    part=m.joined('ジャンパ栓箱・蓋・曲管一体',[box,lid,hinge,pipe],g,'床下グレー')
    part=m.boolean('ジャンパ曲管端をスカートへ合わせる',part,[skirt],1,g,'床下グレー')
    m.entities[skirt]['visibility']='visible';m.mesh_chords[part]=.0015
    contact(m,part,skirt,'ジャンパ曲管端とスカート',area=.05)
    if 'jumpers' not in m.keyparts:m.keyparts['jumpers']=[]
    m.keyparts['jumpers'].append(part)
    t=.65;weights=[(1-t)**3,3*(1-t)**2*t,3*(1-t)*t*t,t**3]
    centre=[sum(w*p[i] for w,p in zip(weights,points)) for i in range(3)]
    for point in [[dx-1.23,y,12.1],centre]:
        m.points.append(dict(entity=part,label='ジャンパ栓箱と曲管の形状',point=point,expectedInside=True))


def manifest(m):
    return dict(revision=REVISION,passNumber=m.appearance_pass,skirtSetbackMm=skirt_setback(m),
        frontFittingsFollowFold=enabled(m,2),
        passengerWindow=dict(zip(['bottomMm','heightMm'],passenger_window(m))),
        rectangularPassengerHandles=enabled(m,3),
        sideWindowLoweringMm=2.4 if enabled(m,4) else 0.,
        sideDestinationSide=-1 if enabled(m,4) else None,
        jumperSocketCount=5 if enabled(m,5) else 0,
        observations=['Relative proportions estimated from original photos; local dimensions are model design estimates, not measured prototype dimensions.'])
