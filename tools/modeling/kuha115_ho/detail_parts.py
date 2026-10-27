"""Photo-proportioned details. Local dimensions are explicit model estimates."""
import math, os, base64
from pathlib import Path


def face_x(y):return math.tan(math.radians(8))*(abs(y)-5.3)


def skirt_mask(m,g):
    """Tapered YZ silhouette with tangent circular corners, extruded along X."""
    vertices=[(-16.0,6.6),(16.0,6.6),(17.1,13.7),(-17.1,13.7)];corners=[];radius=.8
    for i,p in enumerate(vertices):
        before=vertices[i-1];after=vertices[(i+1)%4]
        def unit(q):
            d=(q[0]-p[0],q[1]-p[1]);length=math.hypot(*d);return (d[0]/length,d[1]/length)
        a,b=unit(before),unit(after);angle=math.acos(a[0]*b[0]+a[1]*b[1]);distance=radius/math.tan(angle/2)
        start=(p[0]+a[0]*distance,p[1]+a[1]*distance);end=(p[0]+b[0]*distance,p[1]+b[1]*distance)
        bisector=(a[0]+b[0],a[1]+b[1]);length=math.hypot(*bisector)
        offset=radius/math.sin(angle/2);center=(p[0]+bisector[0]*offset/length,p[1]+bisector[1]*offset/length)
        first=math.atan2(start[1]-center[1],start[0]-center[0]);last=math.atan2(end[1]-center[1],end[0]-center[0])
        corners.append((start,end,center,first,(last-first)%(2*math.pi)))
    segments=[]
    for i,(start,end,center,first,sweep) in enumerate(corners):
        segments.append(m.arc((-2,*center),(1,0,0),(0,1,0),radius,first,sweep))
        segments.append(m.line((-2,*end),(-2,*corners[(i+1)%4][0])))
    wire=m.wire('下絞りと丸隅を持つスカート外形',segments,g)
    return m.extrude('スカートの下絞り外形マスク',[wire],(1,0,0),5,g)


def wiper(m,g,side):
    def p(y,z,stand=.55):return (face_x(y)-stand,side*y,z)
    n=(-math.cos(math.radians(8)),side*math.sin(math.radians(8)),0)
    def strip(name,a,b,width):
        dy,dz=b[0]-a[0],b[1]-a[1];length=math.hypot(dy,dz);oy,oz=-dz*width/(2*length),dy*width/(2*length)
        wire=m.poly(name+'輪郭',[p(a[0]+oy,a[1]+oz),p(b[0]+oy,b[1]+oz),
            p(b[0]-oy,b[1]-oz),p(a[0]-oy,a[1]-oz)],g)
        return m.extrude(name,[wire],n,.18,g)
    arms=[strip('内向きワイパー主腕',(11.1,30.0),(8.5,34.5),.24),
          strip('内向きワイパー連動腕',(11.55,30.0),(8.5,34.5),.20),
          m.cylinder('ワイパー先端継手',p(8.5,34.5),n,.22,.18,g)]
    pivot=m.cylinder('ワイパー支点',p(11.325,30.0),n,.36,.18,g)
    arms=m.joined('銀色ワイパー腕・支点',arms+[pivot],g,'アルミ銀')
    blade=m.tube('ワイパー黒ブレード',p(8.5,34.5,.61),p(7.0,36.4,.61),.13,g)
    blade=m.boolean('腕に合わせたワイパーブレード',blade,[arms],1,g,'黒')
    m.entities[arms]['visibility']='visible';m.clear(arms,blade,'ワイパーの腕と黒ブレード')


def gangway_fittings(m,g):
    # Painted grab rails clear the door face and do not become grooves in it.
    for z,w in [(30.5,6.3),(26.8,3.5)]:
        p=m.tube('貫通扉の横手掛け',(-.53,-w/2,z),(-.53,w/2,z),.10,g)
        feet=[m.tube('手掛けの取付足',(-.53,y,z),(-.22,y,z),.10,g) for y in [-w/2,w/2]]
        m.joined('貫通扉の横手掛け・足付き',[p]+feet,g)
    o=m.poly('渡り板の斜め上隅',[(-.95,-4.15,15.62),(-.95,4.15,15.62),
        (-.95,4.15,17.95),(-.95,3.3,19.2),(-.95,-3.3,19.2),(-.95,-4.15,17.95)],g)
    m.finish(m.extrude('斜め隅の渡り板',[o],(1,0,0),.70,g),thickness=.70)
    for y in [-3.3,-1.65,1.65,3.3]:
        m.finish(m.box('渡り板ヒンジ',-1.13,y-.13,15.65,.16,.26,.55,g),'アルミ銀',.16)
    for side in [-1,1]:
        for z in [20.6,28.9,35.8]:
            m.finish(m.box('幌受の締結金具',-.93,side*4.99-.13,z,.18,.26,.48,g),'アルミ銀',.18)


def lamp(m,g,side,name,y,z,r,color):
    from refinements import enabled
    from fit_details import contact
    n=(-math.cos(math.radians(8)),side*math.sin(math.radians(8)),0)
    xx=face_x(y)
    def at(d):return (xx+n[0]*d,side*y+n[1]*d,z)
    depth=1.15 if name=='前照灯' else .60
    inner_radius=r-.10
    seated=enabled(m,4);optics=enabled(m,5)
    body=m.ring(name+'塗装胴',at(-.35 if seated else .15),n,r+.12,inner_radius,depth+(.5 if seated else 0),g)
    if seated:
        shell=m.finished['body']
        body=m.boolean(name+'の背面を折妻外板に密着',body,[shell],1,g)
        m.entities[shell]['visibility']='visible';m.clear(body,shell,name+'と前面外板')
        contact(m,body,shell,name+'の胴背面と外板の接着',.2)
        tangent=(n[1],-n[0],0)
        for d,inside in [(-.08,False),(.08,True)]:
            m.points.append(dict(entity=body,label=name+'の背面の空間と残存肉厚',
                point=[v+(r+.01)*t for v,t in zip(at(d),tangent)],expectedInside=inside))
    m.finish(body,'アドバンスブルー' if name=='前照灯' else 'フォギーグレー')
    ring=m.ring(name+'前縁金属リング',at(depth+(.15 if optics else .17)),n,r+.11,inner_radius,.12,g)
    m.finish(ring,'アルミ銀',.12);m.clear(body,ring,name+'胴と金属リング')
    if optics:contact(m,body,ring,name+'胴と前縁リングの連続した端面',.2)
    if name=='前照灯':
        outer=[m.circle('反射鏡外側断面',at(d),n,rr,g) for d,rr in
               [(.25,.46),(.45,.76),(.78,1.08),(1.22,r-.12)]]
        inner=[m.circle('反射鏡内側断面',at(d),n,rr,g) for d,rr in
               [(.38,.34),(.55,.65),(.87,1.00),(1.30,r-.19)]]
        reflector=m.loft('椀状反射鏡の外側',outer,g)
        cavity=m.loft('椀状反射鏡の凹部',inner,g)
        reflector=m.boolean('椀状の前照灯反射鏡',reflector,[cavity],1,g,'金属銀')
        if optics:
            lip=m.ring('反射鏡を胴内壁で受ける環状座',at(1.15),n,inner_radius,1.25,.07,g)
            reflector=m.joined('反射鏡・環状接着座',[reflector,lip],g,'金属銀')
            m.clear(body,reflector,'前照灯胴と反射鏡');contact(m,body,reflector,'反射鏡の環状座と胴内壁',.2)
        bulb=m.cylinder(name+'電球',at(.38),n,.24,.24,g)
        m.finish(bulb,'電球色',.24)
        if optics:contact(m,bulb,reflector,'前照灯電球と反射鏡底面',.05)
        # Keep the clear face shallowly convex; the centre cap is only 0.2 mm wide.
        section_data=[(.21,inner_radius),(.27,1.13),(.31,.45),(.32,.10)] if optics else [(.13,r-.11),(.20,r-.16),(.27,1.13),(.31,.45),(.32,.10)]
        sections=[m.circle('凸レンズ断面',at(depth+d),n,rr,g) for d,rr in section_data]
        lens=m.loft('前照灯の浅い凸レンズ',sections,g)
        if optics:
            collar=m.cylinder('レンズを金属枠で受ける円筒縁',at(depth+.13),n,inner_radius,.08,g)
            lens=m.joined('凸レンズ・枠内の円筒受け',[lens,collar],g,color)
        m.finish(lens,color,.12);m.mesh_chords[lens]=.001
        m.mesh_chords[reflector]=.001
    else:
        reflector=m.cylinder(name+'奥の反射鏡',at(.26),n,inner_radius if optics else r-.27,.10,g)
        m.finish(reflector,'金属銀',.1)
        bulb=m.cylinder(name+'電球',at(.36 if optics else .38),n,.18,.18 if optics else .16,g)
        m.finish(bulb,'電球色',.16)
        lens=m.cylinder(name+'透明レンズ',at(depth+.13),n,inner_radius if optics else r-.11,.12,g)
        m.finish(lens,color,.12)
        if optics:
            m.clear(body,reflector,'尾灯胴と反射鏡');contact(m,body,reflector,'尾灯反射鏡と胴内壁',.05)
            contact(m,bulb,reflector,'尾灯電球と反射鏡底面',.05)
    if optics:
        m.clear(ring,lens,name+'金属枠とレンズ');contact(m,ring,lens,name+'レンズの円筒縁と金属枠',.1)
    if seated:
        if not hasattr(m,'lamp_stacks'):m.lamp_stacks=[]
        m.lamp_stacks.append(dict(name=name,side=side,body=body,ring=ring,lens=lens,reflector=reflector,bulb=bulb))


def destination(m,g,roof_outer):
    from refinements import enabled
    from fit_details import contact
    o=m.rounded('表示器ハウジング外',(-.85,-4.65,39.65),(0,1,0),(0,0,1),9.3,3.95,.6,g)
    q=m.rounded('表示器ハウジング開口',(-.85,-4.25,40.08),(0,1,0),(0,0,1),8.5,3.09,.35,g)
    housing=m.extrude('前面表示器ハウジング素材',[o,q],(1,0,0),4.8,g)
    housing=m.boolean('表示箱背面を丸い額に合わせる',housing,[roof_outer],1,g)
    m.mesh_chords[housing]=.006
    x,thickness=(-.78,.2) if enabled(m,3) else (-.12,.05)
    p=m.rounded('前面表示面',(x,-4.25,40.08),(0,1,0),(0,0,1),8.5,3.09,.35,g)
    board=m.finish(m.extrude('前面表示面',[p],(1,0,0),thickness,g),'黒',thickness)
    if enabled(m,3):
        m.clear(housing,board,'表示幕と枠');m.clear(m.finished['roof'],housing,'表示器と屋根')
        contact(m,board,housing,'表示幕の周囲がハウジングへ接する',.5)
        contact(m,housing,m.finished['roof'],'表示箱の背面が額に接する',.5)
        m.points.extend([dict(entity=board,label='表示幕の前面位置と厚さ',point=[x+thickness/2,0,41.5],expectedInside=True),
            dict(entity=board,label='表示幕の前方空間',point=[x-.05,0,41.5],expectedInside=False)])
        m.keyparts['frontDestination']=[housing,board]


def climate_unit(m,g,x,z):
    half=13.1875;rise=3.15;top=4.55;r=(half*half+rise*rise)/(2*rise)
    zc=z+top-r;angle=math.acos(half/r)
    p=m.wire('AU75Gの幅方向アーチ断面',[
        m.line((x,-half,z),(x,half,z)),m.line((x,half,z),(x,half,z+top-rise)),
        m.arc((x,0,zc),(1,0,0),(0,1,0),r,angle,math.pi-2*angle),
        m.line((x,-half,z+top-rise),(x,-half,z))],g)
    ac=m.extrude('AU75Gの長手へ押し出したアーチキセ',[p],(1,0,0),45.75,g)
    slots=[]
    for side in [-1,1]:
        for start in [2,31]:
            for j in range(16):
                slots.append(m.rounded('キセ肩面の通風スリット',(x+start+j*.72,-14 if side<0 else 8.8,z+2.5),
                    (1,0,0),(0,1,0),.34,5.2,.12,g))
    ac=m.cut('AU75Gの肩に実際の通風口',ac,slots,(0,0,1),2.1,g,'アルミ銀')
    ribs=[]
    for dx in [.4,22.8,45.1]:
        inner,outer=r-.02,r+.15
        def pt(rad,a):return (x+dx,rad*math.cos(a),zc+rad*math.sin(a))
        strip=m.wire('キセ横断補強帯',[
            m.arc((x+dx,0,zc),(1,0,0),(0,1,0),outer,angle,math.pi-2*angle),
            m.line(pt(outer,math.pi-angle),pt(inner,math.pi-angle)),
            m.arc((x+dx,0,zc),(1,0,0),(0,1,0),inner,math.pi-angle,-math.pi+2*angle),
            m.line(pt(inner,angle),pt(outer,angle))],g)
        ribs.append(m.extrude('AU75Gの補強帯',[strip],(1,0,0),.20,g))
    ac=m.joined('AU75Gアーチキセ・肩スリット・補強帯',[ac]+ribs,g,'アルミ銀')
    # Black fan rooms and blades sit below, rather than on top of, the grilles.
    m.keyparts['climateFans']=[]
    for cx in [x+8.5,x+37.25]:
        bits=[m.cylinder('ファンハブ',(cx,0,z+3.86),(0,0,1),.65,.24,g)]
        bits.extend([m.box('ファンの支持桟',cx-3.8,-.15,z+3.83,7.6,.30,.16,g),
                     m.box('ファンの支持桟',cx-.15,-3.8,z+3.83,.30,7.6,.16,g)])
        for j in range(8):
            a=j*math.pi/4
            def p(rr,t):return (cx+rr*math.cos(a)-t*math.sin(a),rr*math.sin(a)+t*math.cos(a),z+3.87)
            wire=m.poly('ファン羽根',[p(.45,-.22),p(3.25,-.05),p(3.0,.75),p(.5,.30)],g)
            bits.append(m.extrude('奥まったファン羽根',[wire],(0,0,1),.11,g))
        m.keyparts['climateFans'].append(m.joined('AU75Gの支持桟付きファン',bits,g,'黒'))
    return ac


def ventilator(m,g,x):
    def limit(name,start,end,front,rear):
        wire=m.poly(name,[(start,-4,44.5),(end,-4,44.5),(end,-4,rear),(start,-4,front)],g)
        return m.extrude(name,[wire],(0,1,0),8,g)
    outer=m.rounded('通風器の丸い覆い断面',(x-3.6,-3.25,45.15),(0,1,0),(0,0,1),6.5,2.9,1.0,g)
    outer=m.extrude('通風器の円弧面',[outer],(1,0,0),7.2,g)
    outer=m.boolean('後ろへ低くなる外覆い',outer,[limit('覆いの長手勾配',x-3.6,x+3.6,48.05,47.4)],2,g,'屋根グレー')
    inner=m.rounded('通風器の丸い内側断面',(x-3.7,-2.85,45.5),(0,1,0),(0,0,1),5.7,2.15,.65,g)
    inner=m.extrude('通風器の内側円弧面',[inner],(1,0,0),7.4,g)
    inner=m.boolean('後ろへ低くなる内部空洞',inner,[limit('空洞の長手勾配',x-3.7,x+3.7,47.66,46.99)],2,g,'屋根グレー')
    cover=m.boolean('円弧と平面で構成する湾曲覆い',outer,[inner],1,g,'屋根グレー')
    ribs=[m.box('入口の縦仕切',x-3.58,y,45.45,7.16,.18,2.1,g) for y in [-1.89,-.69,.51,1.71]]
    plate=m.box('通風器取付台',x-3.85,-3.45,45.35,7.7,6.9,.45,g)
    bolts=[m.cylinder('通風器取付ボルト',(xx,yy,45.72),(0,0,1),.16,.22,g)
           for xx in [x-3.4,x+3.4] for yy in [-3.18,3.18]]
    return m.joined('湾曲通風器・五区画入口・取付台',[cover,plate]+ribs+bolts,g,'屋根グレー')


def sideframe(m,g,cx,y,side):
    outer=[m.line((cx-17,y,7.3),(cx-16.425,y,8.1)),
        m.arc((cx-13.125,y,8.1),(0,-1,0),(1,0,0),3.3,math.pi,-math.pi),
        m.line((cx-9.825,y,8.1),(cx-5.2,y,10.3)),m.line((cx-5.2,y,10.3),(cx+5.2,y,10.3)),
        m.line((cx+5.2,y,10.3),(cx+9.825,y,8.1)),
        m.arc((cx+13.125,y,8.1),(0,-1,0),(1,0,0),3.3,math.pi,-math.pi),
        m.line((cx+16.425,y,8.1),(cx+17,y,7.3))]
    pts=[(cx+17,y,7.3),(cx+10,y,7.0),(cx+5.5,y,8.2),(cx-5.5,y,8.2),(cx-10,y,7.0),(cx-17,y,7.3)]
    outer += [m.line(a,b) for a,b in zip(pts,pts[1:])]
    p=m.wire('丸い軸ばね室を持つ側枠外周',outer,g)
    a=m.extrude('TR62系の厚い曲線側枠',[p],(0,-side,0),1.3,g)
    holes=[]
    for dx in [-13.125,13.125]:
        xx=cx+dx
        wire=m.wire('軸箱上の大きなアーチ開口',[
            m.line((xx-2.1,y+side*.1,7.75),(xx+2.1,y+side*.1,7.75)),
            m.line((xx+2.1,y+side*.1,7.75),(xx+2.1,y+side*.1,8.3)),
            m.arc((xx,y+side*.1,8.3),(0,-1,0),(1,0,0),2.1,0,math.pi),
            m.line((xx-2.1,y+side*.1,8.3),(xx-2.1,y+side*.1,7.75))],g)
        holes.append(wire)
    return m.cut('側枠の空間',a,holes,(0,-side,0),1.6,g,'黒')


def lettering(m,root,output):
    """Paint decals are native reference images, never fictitious printable solids."""
    from PIL import Image, ImageDraw, ImageFont
    font=Path(os.environ.get('WINDIR',''))/'Fonts/meiryo.ttc'
    if not font.exists():
        choices=[Path('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc'),
                 Path('/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc')]
        font=next((p for p in choices if p.exists()),None)
    if font is None or not font.exists():raise ValueError('A Japanese font is required for the detailed model decals')
    g=m.group('13 塗装標記｜普通幕・車番（印刷/デカール）',root);m.decals=[]
    def label(name,text,width,height,origin,u,v,fill,pixels=800):
        h=round(pixels*height/width);im=Image.new('RGBA',(pixels,h),(0,0,0,0));draw=ImageDraw.Draw(im)
        f=ImageFont.truetype(str(font),h);box=draw.textbbox((0,0),text,font=f)
        scale=min((pixels-16)/(box[2]-box[0]),(h-8)/(box[3]-box[1]));f=ImageFont.truetype(str(font),max(1,round(h*scale)))
        box=draw.textbbox((0,0),text,font=f);draw.text(((pixels-box[2]+box[0])/2-box[0],(h-box[3]+box[1])/2-box[1]),text,font=f,fill=fill)
        path=output/(name+'.png');im.save(path)
        entity=m.image(name,path,g,origin,u,v,(0,h),width/pixels);m.entities[entity]['visibility']='visible'
        m.decals.append(dict(name=name,pngBase64=base64.b64encode(path.read_bytes()).decode(),origin=list(origin),
            u=list(u),v=list(v),widthMm=width,heightMm=h*width/pixels,entity=entity))
    from refinements import enabled
    label('前面普通幕','普 通',7.8,2.7,(-.805 if enabled(m,3) else -.145,3.9,40.28),(0,-1,0),(0,0,1),(238,235,218,255))
    for side in [-1,1]:
        z=19.4;slope=1.1/10.2;norm=math.sqrt(1+slope*slope)
        y=side*(18.125-slope*(24-z)+.01)
        label('車番標記'+str(side),'クハ115-1222',14.5,1.1,(94 if side<0 else 108.5,y,z),
              (1 if side<0 else -1,0,0),(0,side*slope/norm,1/norm),(53,60,62,255))
