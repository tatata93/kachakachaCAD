"""New, editable 1:80 Kuha115-1000 design; no previous train document is imported."""
from pathlib import Path
import argparse, base64, copy, hashlib, json, math, sys, uuid

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/modeling'))
from native_kcd import Model, expr, vec

SCALE = 80
L, Y, FLOOR, EAVE, CROWN = 243.75, 18.4375, 15.3125, 39.35, 45.675
CORNER, WALL = 3.75, 1.0
PIVOTS = [35.625, 208.125]  # 13,800 mm, adopted from the supplied drawing, not independently measured.
DOORS = [53.8, 131.0, 201.3]
# Photo-proportioned positions; retain the actual glass/sash construction at every wide window.
WINDOWS = [(36.5,8.7),(69.5,8.7),(84.0,13.5),(101.5,13.5),(116.0,8.7),
           (147.0,8.7),(161.5,13.5),(179.0,13.5),(189.4,5.5),(218.0,8.7),(232.0,13.5)]
NS = uuid.UUID('04b8d452-f655-4890-ae91-115100008024')


class Train(Model):
    def id(self):
        self.i += 1
        return str(uuid.uuid5(NS, str(self.i)))

    def __init__(self):
        super().__init__('NEW クハ115-1000 長野色 1/80 HO16.5')
        self.d['documentId'] = str(uuid.uuid5(NS, 'new-document'))
        self.d['metadata'].update(author='kachakachaCAD', description='New native feature design, 1/80, HO 16.5 mm. Supplied AI drawing cross-checked against primary sources. Estimated details remain explicitly identified.')
        self.colors = {}; self.checks = []; self.finished = {}; self.points = []; self.mesh_chords = {}

    def finish(self, p, color='フォギーグレー', thickness=None, notes=''):
        props = dict(materialName='光造形レジン', colorName=color, processName='3Dプリント・塗装・接着',
                     notes=notes, layerCount=1, referenceScaleDenominator=80)
        if thickness is not None: props['nominalThicknessMm'] = thickness
        self.entities[p]['partProperties']['manufacturing'] = props
        self.colors[p] = color
        return p

    def boolean(self, name, target, tools, mode, g, color='フォギーグレー'):
        p = self.add(name, 'part', 'boolean', dict(mode=mode, targets=[target], tools=tools), g, [target]+tools)
        for x in [target]+tools: self.entities[x]['visibility'] = 'hidden'
        return self.finish(p, color)

    def cut(self, name, target, profiles, axis, distance, g, color='フォギーグレー'):
        p = self.add(name, 'part', 'extrude', dict(profiles=profiles, direction=vec(axis), distance=expr(distance),
                     extentMode=0, booleanMode=2, targets=[target]), g, profiles+[target])
        self.entities[target]['visibility'] = 'hidden'
        return self.finish(p, color)

    def loft(self, name, wires, g):
        return self.add(name, 'part', 'create_solid', dict(method=1, profiles=wires, axis=None, path=[],
                        angleRad=2*math.pi, symmetric=False, booleanMode=0, targets=[]), g, wires)

    def perimeter(self, z, inset=0, nose=0, breadth=Y):
        a, b, y, r = nose+inset, L-inset, breadth-inset, CORNER-inset
        return [self.line((a+r,-y,z),(b,-y,z)), self.line((b,-y,z),(b,y,z)),
                self.line((b,y,z),(a+r,y,z)),
                self.arc((a+r,y-r,z),(0,0,1),(1,0,0),r,math.pi/2,math.pi/2),
                self.line((a,y-r,z),(a,-y+r,z)),
                self.arc((a+r,-y+r,z),(0,0,1),(1,0,0),r,math.pi,math.pi/2)]

    def layer(self, name, g, outer, inner, low=13.8, high=39.35):
        o = self.wire(name+'外周', self.perimeter(low,outer),g)
        i = self.wire(name+'内周', self.perimeter(low,inner),g)
        return self.extrude(name,[o,i],(0,0,1),high-low,g)

    def image(self, name, image, g, origin, u, v, pixel, mmpp):
        from PIL import Image
        w,h = Image.open(image).size
        return self.add(name,'image','create_image',dict(pngBase64=base64.b64encode(image.read_bytes()).decode(),
            pixelWidth=w,pixelHeight=h,mmPerPixel=mmpp,rotationRad=0,opacity=.35,mirrorHorizontal=False,
            followSurface=False,faceBrep='',origin=vec(origin),uAxis=vec(u),vAxis=vec(v),
            anchorPixel=vec((*pixel,0)),anchorUv=vec((0,0,0)),uvMetric=vec((1,1,0))),g,visible=False)


def body(m, root):
    g = m.group('01 車体｜窓・扉を実開口',root)
    # Analytic planar sides and circular front quarters preserve the exact envelope.
    outer=m.wire('外板断面',m.perimeter(14.0),g)
    inner=m.wire('内板断面',m.perimeter(14.0,WALL),g)
    a=m.extrude('車体中空外板',[outer,inner],(0,0,1),EAVE-14.0,g)
    for sign, label in [(1,'右'),(-1,'左')]:
        holes=[]
        for i,(x,w) in enumerate(WINDOWS):
            z,h = 27.0,10.5
            if sign==-1 and i==10: x,w,z,h=232.1,6.2,30.2,6.4
            holes.append(m.rounded(label+f'客室開口{i+1}',(x-w/2,sign*(Y+1),z),(1,0,0),(0,0,1),w,h,.55,g))
        for x in DOORS:
            holes.append(m.rounded(label+'客扉開口',(x-8.125,sign*(Y+1),FLOOR),(1,0,0),(0,0,1),16.25,22.8,.35,g))
        holes += [m.rounded(label+'乗務員扉開口',(23.5,sign*(Y+1),FLOOR),(1,0,0),(0,0,1),6.8,22.3,.4,g),
                  m.rounded(label+'運転室窓開口',(4.9,sign*(Y+1),30.7),(1,0,0),(0,0,1),9.2,7.3,.6,g),
                  m.rounded(label+'運転室後窓開口',(16.0,sign*(Y+1),31.0),(1,0,0),(0,0,1),4.3,6.8,.4,g)]
        a=m.cut(label+'側面開口を抜く',a,holes,(0,-sign,0),4,g)
    holes=[m.rounded('前面窓開口',(-3,y,29.15),(0,1,0),(0,0,1),13.46,8.95,.8,g) for y in [-18.26,4.8]]
    holes.append(m.rounded('前面貫通口',(-3,-4.35,FLOOR),(0,1,0),(0,0,1),8.7,23.15,.65,g))
    for i,hole in enumerate(holes):a=m.cut('前面開口を個別に抜く'+str(i+1),a,[hole],(1,0,0),8,g)
    p=m.rounded('後妻貫通口',(L+1,-4.35,FLOOR),(0,1,0),(0,0,1),8.7,23.15,.65,g)
    a=m.cut('後妻貫通口を抜く',a,[p],(-1,0,0),3,g)
    m.finished['body']=a
    return m.finish(a,thickness=WALL,notes='前面ガラスはY±18.26まで回る。前隅曲率は写真からの推定。裾絞りは簡略。')


def side_windows(m,root):
    g=m.group('03 客室サッシ｜上下窓・中桟',root); glass=m.group('04 ガラス｜透明部品',root)
    dg=m.group('05 客扉・乗務員扉・雨樋',root)
    for sign,label in [(1,'右'),(-1,'左')]:
        for i,(x,w) in enumerate(WINDOWS):
            z,h=27,10.5
            if sign==-1 and i==10:x,w,z,h=232.1,6.2,30.2,6.4
            # Thin but connected rim and meeting rail; no louvre substitutes for glass.
            pos=sign*(Y+.43)
            o=m.rounded(label+f'窓枠{i+1}外周',(x-w/2-.18,pos,z-.18),(1,0,0),(0,0,1),w+.36,h+.36,.67,g)
            q=m.rounded(label+f'窓枠{i+1}内周',(x-w/2+.22,pos,z+.22),(1,0,0),(0,0,1),w-.44,h-.44,.4,g)
            a=m.extrude(label+f'サッシ{i+1}',[o,q],(0,-sign,0),.4,g)
            if h>8 and w>10:
                rail=m.box('上下窓の中桟',x-w/2+.15,pos-(.4 if sign>0 else 0),z+h*.51,w-.3,.4,.38,g)
                a=m.boolean(label+f'上下窓サッシ{i+1}',a,[rail],0,g,'アルミ銀')
            m.finish(a,'アルミ銀',.4)
            m.checks.append(dict(first=m.finished['body'],second=a,label=label+f'サッシ{i+1}と外板',noOverlap=True))
            p=m.rounded(label+f'裏貼ガラス{i+1}',(x-w/2-.25,sign*(Y-WALL-.15),z-.25),(1,0,0),(0,0,1),w+.5,h+.5,.7,glass)
            pane=m.finish(m.extrude(label+f'客室透明板{i+1}',[p],(0,-sign,0),.45,glass),'透明',.45)
            m.checks.append(dict(first=m.finished['body'],second=pane,label=label+f'客室ガラス{i+1}',noOverlap=True))
        for i,x in enumerate(DOORS):
            for half in [-1,1]:
                x0=x-7.95 if half<0 else x+.10
                o=m.rounded('客扉葉',(x0,sign*(Y-.25),FLOOR+.15),(1,0,0),(0,0,1),7.85,22.5,.3,dg)
                q=m.rounded('客扉ガラス穴',(x0+1.3,sign*(Y-.25),28.0),(1,0,0),(0,0,1),5.25,8.5,.6,dg)
                m.finish(m.extrude(label+f'客扉{i+1}葉{half}',[o,q],(0,-sign,0),.65,dg),thickness=.65)
                p=m.rounded('客扉透明板',(x0+1.1,sign*(Y-1.15),27.8),(1,0,0),(0,0,1),5.65,8.9,.75,glass)
                m.finish(m.extrude(label+'客扉ガラス',[p],(0,-sign,0),.45,glass),'透明',.45)
        o=m.rounded('乗務員扉葉',(23.65,sign*(Y-.3),FLOOR+.15),(1,0,0),(0,0,1),6.5,22,.4,dg)
        q=m.rounded('乗務員扉ガラス穴',(24.6,sign*(Y-.3),29.4),(1,0,0),(0,0,1),4.6,7.0,.45,dg)
        m.finish(m.extrude(label+'乗務員扉',[o,q],(0,-sign,0),.65,dg),thickness=.65)
        for x,z,w,h in [(4.65,30.45,9.7,7.8),(15.75,30.75,4.8,7.3),(24.4,29.2,5.0,7.4)]:
            p=m.rounded('運転室透明板',(x,sign*(Y-1.15),z),(1,0,0),(0,0,1),w,h,.65,glass)
            pane=m.finish(m.extrude(label+'運転室ガラス',[p],(0,-sign,0),.45,glass),'透明',.45)
            if x==4.65:m.finished['cabGlass'+str(sign)]=pane
        m.finished['gutter'+str(sign)]=m.finish(m.box(label+'雨樋',5,sign*Y-.2,EAVE-.45,L-5,.4,.4,dg),thickness=.4)
        for x in [22.5,31.2]:m.finish(m.tube(label+'乗務員手すり',(x,sign*(Y+.25),22),(x,sign*(Y+.25),29),.2,dg),'アドバンスブルー',.4)


def front(m,root):
    g=m.group('06 前面｜パノラミック窓・Hゴム・灯具',root)
    for side,y in [(-1,-18.26),(1,4.8)]:
        for name,outer,inner,color in [('Hゴム',-.31,-.03,'黒'),('曲面ガラス',1.4,1.85,'透明')]:
            base=m.layer(name+'前頭曲面素材',g,outer,inner,28.8,38.5)
            o=m.rounded(name+'切り出し外形',(-3,y-.2,28.95),(0,1,0),(0,0,1),13.86,9.35,1.0,g)
            profiles=[o]
            if name=='Hゴム':
                profiles.append(m.rounded('ゴムのガラス開口',(-3,y+.18,29.33),(0,1,0),(0,0,1),13.10,8.59,.65,g))
            beam=m.extrude(name+'切り出し工具',profiles,(1,0,0),7.4,g)
            p=m.boolean(('左' if side<0 else '右')+name+'側面まで連続',base,[beam],2,g,color)
            if name=='Hゴム':m.checks.append(dict(first=m.finished['body'],second=p,label='前頭Hゴム'+str(side),noOverlap=True))
            if name=='曲面ガラス':
                m.checks.append(dict(first=m.finished['body'],second=p,label='前頭曲面ガラス'+str(side),noOverlap=True))
                m.checks.append(dict(first=m.finished['cabGlass'+str(side)],second=p,label='前頭と運転室側面のガラス'+str(side),noOverlap=True))
        m.finish(m.tube('ワイパーアーム',(-.35,side*8.0,29.5),(-.35,side*10.0,34.3),.18,g),'黒',.36)
        m.finish(m.tube('ワイパーブレード',(-.45,side*8.3,34.0),(-.45,side*12.1,35.2),.18,g),'黒',.36)
    o=m.rounded('前面貫通扉',(-.2,-4.2,FLOOR+.15),(0,1,0),(0,0,1),8.4,22.85,.5,g)
    q=m.rounded('貫通扉窓',(-.2,-2.9,29.0),(0,1,0),(0,0,1),5.8,8.3,.55,g)
    m.finish(m.extrude('貫通扉',[o,q],(1,0,0),.7,g),thickness=.7)
    o=m.rounded('貫通扉Hゴム外',(-.55,-3.1,28.8),(0,1,0),(0,0,1),6.2,8.7,.7,g)
    q=m.rounded('貫通扉Hゴム内',(-.55,-2.75,29.15),(0,1,0),(0,0,1),5.5,8.0,.4,g)
    m.finish(m.extrude('貫通扉窓Hゴム',[o,q],(1,0,0),.3,g),'黒',.3)
    p=m.rounded('貫通扉透明板',(.65,-3.1,28.8),(0,1,0),(0,0,1),6.2,8.7,.7,g)
    m.finish(m.extrude('貫通扉ガラス',[p],(1,0,0),.45,g),'透明',.45)
    m.finish(m.box('貫通扉取手',-.55,3.0,23.2,.45,.4,1.4,g),'アルミ銀',.4)
    for side in [-1,1]:
        for name,z,r,col in [('前照灯',25.5,1.65,'透明'),('尾灯',20.9,.85,'赤透明')]:
            m.finish(m.ring(name+'リム',(-.6,side*12.4,z),(1,0,0),r,r-.3,.75,g),'アルミ銀',.3)
            m.finish(m.cylinder(name+'レンズ',(-.65,side*12.4,z),(1,0,0),r-.34,.45,g),col,.45)
        m.finish(m.box('タイフォン シャッターカバー',-.5,side*9.5-.6,24.7,.45,1.2,1.7,g),thickness=.45)
    # Destination aperture above the gangway, and a complete gangway rim, separate from the body.
    o=m.rounded('幌枠外',(-.65,-4.8,15.1),(0,1,0),(0,0,1),9.6,24.0,.65,g)
    q=m.rounded('幌枠内',(-.65,-4.35,15.55),(0,1,0),(0,0,1),8.7,23.1,.4,g)
    m.finish(m.extrude('貫通幌枠',[o,q],(1,0,0),.45,g),'フォギーグレー',.45)
    m.finish(m.box('渡り板',-1.1,-4.25,14.8,1.6,8.5,.5,g),'アドバンスブルー',.5)
    rg=m.group('後妻貫通扉・幌枠',g)
    o=m.rounded('後妻扉外形',(L+.1,-4.2,FLOOR+.15),(0,1,0),(0,0,1),8.4,22.85,.5,rg)
    q=m.rounded('後妻扉窓',(L+.1,-2.9,29.0),(0,1,0),(0,0,1),5.8,8.3,.55,rg)
    m.finish(m.extrude('後妻貫通扉',[o,q],(-1,0,0),.7,rg),thickness=.7)
    p=m.rounded('後妻透明板',(L-.85,-3.1,28.8),(0,1,0),(0,0,1),6.2,8.7,.7,rg)
    m.finish(m.extrude('後妻貫通扉ガラス',[p],(-1,0,0),.45,rg),'透明',.45)
    o=m.rounded('後妻幌枠外形',(L+.45,-4.8,15.1),(0,1,0),(0,0,1),9.6,24,.65,rg)
    q=m.rounded('後妻幌枠開口',(L+.45,-4.35,15.55),(0,1,0),(0,0,1),8.7,23.1,.4,rg)
    m.finish(m.extrude('後妻幌枠',[o,q],(-1,0,0),.45,rg),'床下グレー',.45)


def roof(m,root):
    g=m.group('02 屋根｜複曲面ドーム・一定円弧一般部',root)
    def section(x,w,t):
        c=(w*w+EAVE*EAVE-t*t)/(2*(EAVE-t));r=t-c;a=math.asin(w/r)
        # Keep the normal 1 mm roof thickness, but end both arcs on the
        # horizontal body/roof joint. Equal angles leave an inner lip below it.
        inner_a=math.acos((EAVE-c)/(r-1))
        def p(rad,ang):return (x,-rad*math.sin(ang),c+rad*math.cos(ang))
        return m.wire('屋根閉断面',[
            m.arc((x,0,c),(1,0,0),(0,0,1),r,-a,2*a),m.line(p(r,a),p(r-1,inner_a)),
            m.arc((x,0,c),(1,0,0),(0,0,1),r-1,inner_a,-2*inner_a),m.line(p(r-1,-inner_a),p(r,-a))],g)
    stations=[]
    for x in [0,.3,.8,1.5,2.4,3.2,CORNER]:
        f=math.sqrt(max(0,1-(1-x/CORNER)**2))
        stations.append(section(x,Y-CORNER+CORNER*f,43.7+(CROWN-43.7)*f))
    stations.append(section(4.05,Y,CROWN))
    a=m.loft('前頭屋根ドーム 別部品',stations,g)
    m.finish(a,'屋根グレー',1.0,notes='曲面の前頭キャップ。X=4.05で一般部に接着する。')
    dome=a
    main=m.extrude('屋根一般部 円弧一定断面',[stations[-1]],(1,0,0),L-4.05,g)
    m.finished['roof']=m.finish(main,'屋根グレー',1.0)
    # Front and rear closures follow the roof's inner arc instead of square end plates.
    plates=[]
    for x,w,t in [(0,Y-CORNER,43.7),(L-.85,Y,CROWN)]:
        c=(w*w+EAVE*EAVE-t*t)/(2*(EAVE-t));r=t-c-1;a0=math.asin(w/(t-c))
        p0=(x,r*math.sin(a0),c+r*math.cos(a0));p1=(x,-p0[1],p0[2])
        w0=m.wire('妻上部屋根輪郭',[m.arc((x,0,c),(1,0,0),(0,0,1),r,-a0,2*a0),m.line(p1,p0)],g)
        plates.append(m.extrude('妻上部',[w0],(1,0,0),.85,g))
    b=m.boolean('車体 妻上部一体',m.finished['body'],plates,0,m.entities[m.finished['body']]['groupId'])
    m.finished['body']=m.finish(b,thickness=WALL)
    for c in m.checks:
        if c['label'].startswith(('右客室','左客室','前頭曲面','前頭Hゴム','右サッシ','左サッシ')):c['first']=b
    m.checks += [dict(first=b,second=part,label=label,noOverlap=True) for part,label in [(main,'車体と一般屋根'),(dome,'車体と前頭屋根')]]
    fg=m.group('前面表示部',g)
    m.finish(m.box('前面行先表示器枠',-.25,-4.7,40.0,.6,9.4,2.25,fg),thickness=.6)
    m.finish(m.box('前面行先表示窓',-.35,-4.2,40.35,.2,8.4,1.55,fg),'黒',.2)
    eg=m.group('07 屋根機器｜AU75G・押込通風器6個',root)
    # Manufacturer AU75G overall envelope: 3660 x 2110 x 376 mm. Two axial fans.
    x0=L/2-45.75/2; z0=CROWN+.15
    p=m.poly('AU75G 側断面',[(x0,-13.1875,z0),(x0+45.75,-13.1875,z0),(x0+45.75,-13.1875,z0+2.9),
         (x0+42.8,-13.1875,z0+4.7),(x0+2.95,-13.1875,z0+4.7),(x0,-13.1875,z0+2.9)],eg)
    cooler=m.extrude('AU75G 本体',[p],(0,1,0),26.375,eg);details=[]
    holes=[m.circle('室外ファン凹み',(x,0,z0+4.35),(0,0,1),3.3,eg) for x in [x0+7.5,x0+38.25]]
    cooler=m.cut('AU75G ファン開口',cooler,holes,(0,0,1),1,eg,'屋根グレー')
    for x in [x0+7.5,x0+38.25]:
        details.append(m.ring('室外ファン枠',(x,0,z0+4.6),(0,0,1),3.6,3.3,.1,eg))
        for dy in [-2.4,-1.2,0,1.2,2.4]:
            half=math.sqrt(3.5**2-dy**2)
            details.append(m.box('ファン保護桟',x-half,dy-.12,z0+4.6,2*half,.24,.1,eg))
    for sign in [-1,1]:
        for x in [x0+12.5,x0+15,x0+17.5,x0+20,x0+22.5,x0+25,x0+27.5,x0+30,x0+32.5]:
            details.append(m.box('側面冷却格子',x,sign*13.0-.12,z0+.65,.3,.25,2.8,eg))
    for x in [x0+5,x0+36.75]:
        for y in [-8,6.8]:details.append(m.box('AU75G 取付脚',x,y,43.5,4,1.2,2.4,eg))
    cooler=m.boolean('AU75G 2ファン・側面格子・脚',cooler,details,0,eg,'屋根グレー')
    c=(Y*Y+EAVE*EAVE-CROWN*CROWN)/(2*(EAVE-CROWN));r=CROWN-c+.05;a0=math.asin(Y/(CROWN-c))
    p0=(0,r*math.sin(a0),c+r*math.cos(a0));p1=(0,-p0[1],p0[2])
    w=m.wire('屋根外形と接着すき間0.05',[m.arc((0,0,c),(1,0,0),(0,0,1),r,-a0,2*a0),m.line(p1,p0)],eg)
    tool=m.extrude('屋根外形の脚切り工具',[w],(1,0,0),L,eg)
    cooler=m.boolean('AU75G 屋根曲率に合う取付脚付き',cooler,[tool],1,eg,'屋根グレー')
    m.finish(cooler,'屋根グレー',notes='本体外形は製造者寸法。脚底は屋根曲率に沿い接着すき間0.05mm。格子・内部は簡略。')
    m.checks.append(dict(first=m.finished['roof'],second=cooler,label='屋根と冷房装置取付脚',noOverlap=True))
    for x in [31,64,85,169,191,215]:
        p=m.poly('通風器側断面',[(x-3.4,-3,45.3),(x+3.4,-3,45.3),(x+2.6,-3,47.1),(x-2.6,-3,47.1)],eg)
        vent=m.extrude('押込通風器素材',[p],(0,1,0),6,eg)
        vent=m.boolean('押込通風器 屋根曲率に合わせる',vent,[tool],1,eg,'屋根グレー')
        m.finish(vent,'屋根グレー',.6)
        m.mesh_chords[vent]=.005  # Resolve the curved underside without over-meshing the dome.
        m.checks.append(dict(first=main,second=vent,label='屋根と通風器'+str(x),noOverlap=True))
    base=m.finish(m.box('無線アンテナ基部',12,-.7,45.4,1.5,1.4,.7,eg),'屋根グレー')
    mast=m.cylinder('アンテナ支柱',(12.75,0,46.05),(0,0,1),.28,2.3,eg)
    antenna=m.boolean('無線アンテナ一体',base,[mast],0,eg,'屋根グレー')
    antenna=m.boolean('無線アンテナ 屋根曲率に合わせる',antenna,[tool],1,eg,'屋根グレー')
    m.checks.append(dict(first=main,second=antenna,label='屋根とアンテナ',noOverlap=True))
    for y in [-5.5,5.5]:
        item=m.cylinder('信号炎管・ホイッスル素材',(5,y,44.9),(0,0,1),.45,1.9,eg)
        item=m.boolean('信号炎管・ホイッスル 屋根に合わせる',item,[tool],1,eg,'屋根グレー')
        m.checks.append(dict(first=main,second=item,label='屋根と信号炎管等'+str(y),noOverlap=True))


def chassis(m,root):
    fg=m.group('08 床板｜HO16.5・M2台車中心穴',root)
    p=m.wire('床板外形 裾絞りを逃がす',m.perimeter(13.9,1.65,.55),fg)
    holes=[m.circle('台車中心M2.2',(x,0,13.9),(0,0,1),1.1,fg) for x in PIVOTS]
    floor=m.finish(m.extrude('床板',[p]+holes,(0,0,1),1.25,fg),'床下グレー',1.25)
    m.finished['floor']=floor
    m.checks.append(dict(first=m.finished['body'],second=floor,label='車体と床板 全周逃げ',noOverlap=True))
    g=m.group('09 床下・スカート・連結器',root)
    for x,y,w,d,h in [(64,-14,18,9,5.0),(87,-14,13,9,4.6),(109,-14,22,9,4.8),(144,-14,19,9,5.6),
                       (72,5,23,9,5.0),(106,5,18,9,4.7),(150,5,20,9,4.8)]:
        box=m.box('クハ機器箱',x,y,13.9-h,w,d,h,g)
        ribs=[m.box('取付ブラケット',xx,y-.3,13.5,1.0,d+.6,.4,g) for xx in [x+1,x+w-2]]
        m.boolean('床下機器 取付座付き',box,ribs,0,g,'床下グレー')
    for x,y in [(56,-3),(177,0)]:
        tank=m.cylinder('空気溜',(x,y,10.7),(1,0,0),2.0,12,g)
        feet=[m.box('空気溜吊り脚',xx,y-.65,12.1,1.0,1.3,1.8,g) for xx in [x+1,x+10]]
        m.boolean('空気溜 脚一体',tank,feet,0,g,'床下グレー')
    for s in [-1,1]:
        pts=[(-.5,s*3.0,8.0),(-.5,s*16,8.0),(-.5,s*17,13.5),(-.5,s*3.0,13.5)]
        p=m.poly('スカート輪郭',pts,g);skirt=m.extrude('スカート 左右',[p],(1,0,0),.8,g)
        foot=m.box('スカート接着座',-.25,s*8-.65,12.8,3.75,1.3,1.1,g)
        m.boolean('スカート 床板接着座一体',skirt,[foot],0,g,'床下グレー')
    for isfront,x in [(True,-3.125),(False,L+2.325)]:
        head=m.box('連結器頭',x,-1.4,9.7,.8,2.8,2,g)
        stem=m.box('連結器柄',-2.4 if isfront else L-4,-.6,10.0,6.8,1.2,1.2,g)
        mount=m.box('連結器床板取付座',2.1 if isfront else L-3.5,-1.2,10.8,2,2.4,3.1,g)
        m.boolean('連結器 床板取付座一体',head,[stem,mount],0,g,'床下グレー')
    bg=m.group('10 台車｜TR62系外観・輪軸は別部品',root)
    for cx,lab in zip(PIVOTS,['前','後']):
        g=m.group(lab+'台車',bg)
        core=m.box('枕梁',cx-2,-12,8.9,4,24,3.3,g)
        bits=[m.cylinder('心皿',(cx,0,11.8),(0,0,1),2.5,2.1,g)]
        for sign in [-1,1]:
            y=sign*12.0
            pts=[(cx-15,y,6.1),(cx-15,y,9.5),(cx-10,y,10.4),(cx-5,y,9.1),(cx+5,y,9.1),(cx+10,y,10.4),(cx+15,y,9.5),(cx+15,y,6.1),(cx+6,y,6.1),(cx+3,y,7.4),(cx-3,y,7.4),(cx-6,y,6.1)]
            w=m.poly('TR62側枠輪郭',pts,g);bits.append(m.extrude('台車側枠',[w],(0,-sign,0),1.45,g))
            for dx in [-13.125,13.125]:
                bits.append(m.box('密封軸受箱',cx+dx-1.5,y-1.1,4.1,3,2.2,3,g))
                for off in [-2.25,2.25]:bits.append(m.cylinder('軸ばね',(cx+dx+off,y-sign*.2,7),(0,0,1),.7,2.2,g))
        a=m.boolean(lab+'台車枠一体',core,bits,0,g,'床下グレー')
        holes=[m.circle('輪軸受穴',(cx+dx,-14,5.375),(0,1,0),1.1,g) for dx in [-13.125,13.125]]
        a=m.cut(lab+'台車 輪軸受を抜く',a,holes,(0,1,0),28,g,'床下グレー')
        slots=[m.rect('下から輪軸を入れる溝',(cx+dx-1.1,-14,3.8),(1,0,0),(0,0,1),2.2,1.575,g) for dx in [-13.125,13.125]]
        a=m.cut(lab+'台車 下開き軸受',a,slots,(0,1,0),28,g,'床下グレー')
        hole=m.circle('心皿M2.2',(cx,0,8.5),(0,0,1),1.1,g)
        a=m.cut(lab+'台車 心皿穴',a,[hole],(0,0,1),6,g,'床下グレー')
        m.points.append(dict(entity=a,label=lab+'台車の心皿穴 下面の薄膜なし',point=[cx,0,8.95],expectedInside=False))
        keepers=[]
        for y in [-13.25,10.5]:keepers.append(m.finish(m.box(lab+'台車 輪軸押え板',cx-15.5,y,3.5,31,2.75,.6,g),'床下グレー',.6))
        for n,dx in enumerate([-13.125,13.125]):
            x=cx+dx;ax=m.cylinder('輪軸',(x,-12.6,5.375),(0,1,0),.8,25.2,g);wheels=[]
            for sy in [-1,1]:
                wheels.append(m.cylinder('踏面',(x,-10.0 if sy<0 else 8.2,5.375),(0,1,0),5.375,1.8,g))
                wheels.append(m.cylinder('フランジ',(x,-8.2 if sy<0 else 7.25,5.375),(0,1,0),5.65,.95,g))
            ax=m.boolean(lab+f'輪軸{n+1} HO16.5',ax,wheels,0,g,'金属銀')
            m.checks.append(dict(first=a,second=ax,label=lab+f'輪軸{n+1}と軸受クリアランス',noOverlap=True))
            for keeper in keepers:m.checks.append(dict(first=keeper,second=ax,label=lab+f'輪軸{n+1}と押え板',noOverlap=True))
    ig=m.group('11 室内・運転台',root)
    p=m.rect('室内トレー',(2,-16.6,15.15),(1,0,0),(0,1,0),L-4,33.2,ig)
    holes=[m.circle('心皿ねじ逃げ',(x,0,15.15),(0,0,1),1.35,ig) for x in PIVOTS]
    tray=m.extrude('室内トレー',[p]+holes,(0,0,1),.45,ig);bits=[]
    bits.append(m.box('運転室仕切り',31,-16.6,15.5,.8,33.2,23,ig))
    desk=m.box('運転台',4,-15,21.8,6,12,3.0,ig)
    legs=[m.box('運転台脚',4.5,y,15.5,1,1,6.5,ig) for y in [-14,-5]]
    bits.append(m.boolean('運転台脚付き',desk,legs,0,ig,'室内クリーム'))
    for x in [84,101.5,161.5,179]:
        for y in [-15,5.5]:
            bits += [m.box('座席',x-3,y,19.0,6,9.5,1,ig),m.box('背もたれ',x+2.3,y,19.9,.7,9.5,5.6,ig),
                     m.box('座席脚',x-1,y+1,15.5,1,7.5,3.6,ig)]
    bits.append(m.box('便所仕切り',224,-16.6,15.5,.8,12.0,21,ig))
    a=m.boolean('室内 座席・運転室一体トレー',tray,bits,0,ig,'室内クリーム');m.finished['interior']=a


def manifest(m):
    return dict(scale=80,gaugeMm=16.5,stlChordMm=.025,stlChordByEntity=m.mesh_chords,dimensions=dict(bodyLength=L,maximumWidth=2*Y,roofHeight=CROWN,
        floorHeight=FLOOR,coupledLength=250,bogieCentres=172.5),estimated=dict(doorCentres=DOORS,windows=WINDOWS,
        frontCornerRadius=CORNER,axleSpacing=26.25,wheelDiameter=10.75),paint=dict(
        base='フォギーグレー',bands=[dict(zMin=24.7,zMax=26,color='リフレッシュグリーン'),dict(zMin=26,zMax=38.5,color='アドバンスブルー')],
        note='帯位置・RGBは写真に基づく推定。CADは材質名を保存し、画面の個別部品塗色には未対応。'),colors=m.colors,groups=m.d['groups'],
        assemblyChecks=m.checks,pointChecks=m.points,entities=[{k:e[k] for k in ['id','kind','displayName','visibility','groupId','partProperties']} for e in m.d['entities']])


def split(m, root, out):
    s=copy.deepcopy(m);s.d['documentId']=str(uuid.uuid5(NS,'split-document'))
    g=s.group('13 小型プリンタ｜車体・屋根・床板・室内を2分割',root);checks=[];x=L/2
    for key in ['body','roof','floor','interior','gutter-1','gutter1']:
        target=s.finished[key];name=s.entities[target]['displayName'];color=s.colors[target]
        p=s.rect('前半切断',(x,-30,-2),(0,1,0),(0,0,1),60,65,g)
        a=s.cut(name+' 前半',target,[p],(1,0,0),L-x+10,g,color)
        p=s.rect('後半切断',(-5,-30,-2),(0,1,0),(0,0,1),60,65,g)
        b=s.cut(name+' 後半',target,[p],(1,0,0),x+5,g,color)
        checks.append(dict(source=target,front=a,rear=b,label=name))
    s.finish(s.box('床板継ぎ目 添え板',x-6,-4,12.9,12,8,1,g),'床下グレー',1)
    s.save(out/'kuha115-1000-ho80-new-split.kcd2')
    data=manifest(s);data.update(splitAtX=x,splitChecks=checks)
    (out/'design-split.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);ap.add_argument('--reference',type=Path,required=True)
    args=ap.parse_args();out=args.output;out.mkdir(parents=True,exist_ok=False)
    reference=out/'reference-three-view.png';reference.write_bytes(args.reference.read_bytes())
    m=Train();root=m.group('新規設計 クハ115-1000 長野色 1/80 HO16.5')
    body(m,root);side_windows(m,root);front(m,root);roof(m,root);chassis(m,root)
    for sign in [-1,1]:
        for x,z,label,inside in [(x,32,'客室窓の実開口',False) for x,w in WINDOWS]+[(x,20,'客扉の実開口',False) for x in DOORS]+[(75.5,32,'窓間の柱',True),(9.5,34,'運転室窓の実開口',False),(18,34,'運転室後窓の実開口',False),(26.5,20,'乗務員扉の実開口',False)]:
            if sign==-1 and x==232:x,z=232.1,33
            m.points.append(dict(entity=m.finished['body'],label=label+str(sign),point=[x,sign*(Y-.5),z],expectedInside=inside))
    for x in PIVOTS:m.points.append(dict(entity=m.finished['floor'],label='床板の心皿貫通穴',point=[x,0,14.4],expectedInside=False))
    for y in [-8,0,8]:m.points.append(dict(entity=m.finished['body'],label='前面窓・貫通口の実開口',point=[.5,y,33],expectedInside=False))
    m.points.append(dict(entity=m.finished['body'],label='後妻貫通口の実開口',point=[L-.5,0,20],expectedInside=False))
    g=m.group('12 参考図｜元画像・校正基準',root);m.groups[g]['state']='hidden'
    for name,o,u,v,px,mmpp in [('運転士側',(0,32,0),(1,0,0),(0,0,1),(300,635),L/1087),
        ('正面',(-10,0,0),(0,1,0),(0,0,1),(153,635),(2*Y)/164),
        ('上面',(0,0,55),(1,0,0),(0,1,0),(300,229),L/1087)]:
        m.image('添付参考図 '+name,reference,g,o,u,v,px,mmpp)
    m.d['activeGroupId']=root
    m.d['uiState']={'activeMode':'part','theme':'windows95','extensions':{'kuha115New':{'scale':80,'gaugeMm':16.5,'referenceSha256':hashlib.sha256(reference.read_bytes()).hexdigest()}}}
    m.save(out/'kuha115-1000-ho80-new.kcd2')
    (out/'design.json').write_text(json.dumps(manifest(m),ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    split(m,root,out)
    print(json.dumps(dict(entities=len(m.d['entities']),features=len(m.d['features']),
          visibleParts=sum(e['kind']=='part' and e['visibility']=='visible' for e in m.d['entities']),
          groups=len(m.d['groups'])),ensure_ascii=False))


if __name__=='__main__':main()
