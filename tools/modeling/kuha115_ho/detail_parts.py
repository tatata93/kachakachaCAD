"""Photo-proportioned details. Local dimensions are explicit model estimates."""
import math, os, base64
from pathlib import Path


def face_x(y):return math.tan(math.radians(8))*(abs(y)-5.3)


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
    n=(-math.cos(math.radians(8)),side*math.sin(math.radians(8)),0)
    xx=face_x(y)
    def at(d):return (xx+n[0]*d,side*y+n[1]*d,z)
    depth=1.15 if name=='前照灯' else .60
    body=m.ring(name+'塗装胴',at(.15),n,r+.12,r-.22,depth,g)
    m.finish(body,'アドバンスブルー' if name=='前照灯' else 'フォギーグレー')
    ring=m.ring(name+'前縁金属リング',at(depth+.17),n,r+.11,r-.26,.12,g)
    m.finish(ring,'アルミ銀',.12);m.clear(body,ring,name+'胴と金属リング')
    # The aperture has a recessed metallic reflector and a separate clear lens.
    reflector=m.cylinder(name+'奥の反射鏡',at(.36),n,r-.27,.10,g)
    m.finish(reflector,'金属銀',.1)
    bulb=m.cylinder(name+'電球',at(.48),n,.28 if name=='前照灯' else .18,.20,g)
    m.finish(bulb,'電球色',.2)
    lens=m.cylinder(name+'透明レンズ',at(depth+.13),n,r-.28,.12,g)
    m.finish(lens,color,.12)


def destination(m,g,roof_outer):
    o=m.rounded('表示器ハウジング外',(-.85,-4.65,39.65),(0,1,0),(0,0,1),9.3,3.95,.6,g)
    q=m.rounded('表示器ハウジング開口',(-.85,-4.25,40.08),(0,1,0),(0,0,1),8.5,3.09,.35,g)
    housing=m.extrude('前面表示器ハウジング素材',[o,q],(1,0,0),3.8,g)
    m.boolean('表示箱背面を丸い額に合わせる',housing,[roof_outer],1,g)
    p=m.rounded('前面表示面',(-.12,-4.25,40.08),(0,1,0),(0,0,1),8.5,3.09,.35,g)
    m.finish(m.extrude('前面表示面',[p],(1,0,0),.05,g),'黒',.05)


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
    for cx in [x+8.5,x+37.25]:
        bits=[m.cylinder('ファンハブ',(cx,0,z+3.86),(0,0,1),.65,.24,g)]
        for j in range(8):
            a=j*math.pi/4
            def p(rr,t):return (cx+rr*math.cos(a)-t*math.sin(a),rr*math.sin(a)+t*math.cos(a),z+3.87)
            wire=m.poly('ファン羽根',[p(.45,-.22),p(3.25,-.05),p(3.0,.75),p(.5,.30)],g)
            bits.append(m.extrude('奥まったファン羽根',[wire],(0,0,1),.11,g))
        m.joined('AU75Gの暗いファン室・羽根',bits,g,'黒')
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
    label('前面普通幕','普 通',7.8,2.7,(-.145,3.9,40.28),(0,-1,0),(0,0,1),(238,235,218,255))
    for side in [-1,1]:
        z=19.4;slope=1.1/10.2;norm=math.sqrt(1+slope*slope)
        y=side*(18.125-slope*(24-z)+.01)
        label('車番標記'+str(side),'クハ115-1222',14.5,1.1,(94 if side<0 else 108.5,y,z),
              (1 if side<0 else -1,0,0),(0,side*slope/norm,1/norm),(53,60,62,255))
