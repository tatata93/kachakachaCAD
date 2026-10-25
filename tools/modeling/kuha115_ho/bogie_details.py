"""TR62 appearance corrections. Native curves/solids; local dimensions are estimates.

The 2100 mm wheelbase and 860 mm wheel diameter are prototype dimensions.
Brake-disc lateral positions remain HO16.5 adaptations, not 1/80 reproductions.
"""
import math
from native_kcd import vec

REVISION = '2026-10-25-tr62-independent-review'


def spring(m,name,x,y,z,r,height,turns,g):
    """A continuous swept round rib; filled valleys match the reference appearance.

    Four tangent Beziers per turn stay in one path wire. The source path is a
    construction input, not a row of separate printable coils. A single fitted
    B-spline was rejected after the native volume integration check failed.
    """
    wire_r=.28 if r>1.5 else .19
    radius=r-wire_r;rise=(height-2*wire_r)/turns;step=math.pi/2
    k=4/3*math.tan(step/4);dz=rise/4;segments=[];errors=[]
    for i in range(4*turns):
        a=i*step;b=a+step;zz=z+wire_r+i*dz
        points=[(x+radius*math.cos(a),y+radius*math.sin(a),zz),
            (x+radius*(math.cos(a)-k*math.sin(a)),y+radius*(math.sin(a)+k*math.cos(a)),zz+dz/3),
            (x+radius*(math.cos(b)+k*math.sin(b)),y+radius*(math.sin(b)-k*math.cos(b)),zz+2*dz/3),
            (x+radius*math.cos(b),y+radius*math.sin(b),zz+dz)]
        segments.append(dict(id=m.id(),type='cubic_bezier',controlPoints=[vec(p) for p in points]))
        for j in range(65):
            t=j/64;weights=[(1-t)**3,3*(1-t)**2*t,3*(1-t)*t*t,t**3]
            p=[sum(w*q[axis] for w,q in zip(weights,points)) for axis in range(3)]
            angle=math.atan2(p[1]-y,p[0]-x)-a
            angle=(angle+2*math.pi)%(2*math.pi)
            if abs(angle-2*math.pi)<1e-10:angle=0
            ideal=(x+radius*math.cos(a+angle),y+radius*math.sin(a+angle),zz+dz*angle/step)
            errors.append(math.dist(p,ideal))
    maximum=max(errors);rms=math.sqrt(sum(e*e for e in errors)/len(errors));assert maximum<.001
    path=m.wire(name+' 連続した斜めの巻線経路',segments,g)
    tangent=(0,3*k*radius,dz);length=math.sqrt(sum(v*v for v in tangent))
    profile=m.circle(name+' 丸い線材断面',(x+radius,y,z+wire_r),tuple(v/length for v in tangent),wire_r,g)
    if not hasattr(m,'spring_checks'):m.spring_checks=[]
    m.spring_checks.append(dict(path=path,maximumMm=maximum,rmsMm=rms,samples=len(errors),turns=turns,segments=len(segments)))
    coil=m.add(name+' 連続巻線','part','create_solid',dict(method=2,profiles=[profile],axis=None,
        path=[path],angleRad=2*math.pi,symmetric=False,booleanMode=0,targets=[]),g,[profile,path])
    core=m.cylinder(name+' 谷の埋まった覆い',(x,y,z),(0,0,1),r-1.45*wire_r,height,g)
    seats=[m.cylinder(name+' 上下の受座',(x,y,zz),(0,0,1),r+.08,.16,g) for zz in [z,z+height-.16]]
    return m.joined(name,[core,coil]+seats,g,'黒',note='写真の連続した斜めの隆起を再現。谷を埋めた外観。覆いの材質と車番別仕様は未確定。')


def sideframe(m,g,cx,y,side):
    # Flat upper rail with rounded ends, rather than two giant semicircular hoops.
    p=m.rounded('連続した平らな上縁の側枠外周',(cx-17,y,7.15),(1,0,0),(0,0,1),34,4.35,1.2,g)
    frame=m.extrude('TR62系の連続した上梁',[p],(0,-side,0),1.3,g)
    holes=[m.rounded('軸箱上の浅い独立開口',(cx+dx-2.15,y+side*.1,8.05),
        (1,0,0),(0,0,1),4.3,2.5,.85,g) for dx in [-13.125,13.125]]
    # Keep a substantial centre web for the bolster and hanger mounts.
    return m.cut('側枠の空間',frame,holes,(0,-side,0),1.6,g,'黒')


def hanger(m,g,cx,side,dx):
    a=(cx+dx,side*12.7,7.1);b=(cx+dx*.95,side*11.7,11.2)
    # Broad flat suspension link with distinct round end eyes and visible pins.
    p=m.poly('揺れ枕吊りの幅広い板',[(a[0]-.36,a[1],a[2]),(a[0]+.36,a[1],a[2]),
        (b[0]+.36,b[1],b[2]),(b[0]-.36,b[1],b[2])],g)
    plate=m.extrude('揺れ枕吊り板',[p],(0,side,0),.36,g)
    eyes=[m.cylinder('吊りリンクの丸い目',(p[0],p[1]-.08*side,p[2]),(0,side,0),.55,.55,g) for p in [a,b]]
    pins=[m.cylinder('吊りリンクのピン',(p[0],p[1]+.40*side,p[2]),(0,side,0),.24,.23,g) for p in [a,b]]
    return m.joined('板状揺れ枕吊り・目・ピン',[plate]+eyes+pins,g,'黒')


def damper(m,g,cx,side):
    y=side*13.2
    bits=[m.cylinder('縦ダンパー胴',(cx,y,7.3),(0,0,1),.46,2.4,g),
        m.cylinder('縦ダンパーの細いロッド',(cx,y,9.6),(0,0,1),.22,1.3,g)]
    for z in [7.3,10.85]:
        bits += [m.box('ダンパーと側枠の連結耳',cx-.65,min(y,side*11.8)-.25,z-.18,
                      1.3,1.9,.36,g),
                 m.cylinder('ダンパー端の横ピン',(cx,y-.55,z),(0,1,0),.25,1.1,g)]
    return m.joined('胴・ロッド・横ピン付き縦ダンパー',bits,g,'黒')


def brake_bits(m,g,cx):
    """Two inboard cylinders, four two-sided calipers, levers and supply pipes."""
    bits=[]
    for end in [-1,1]:
        # The JNR plan shows a transverse, two-piston cylinder at each transom.
        # Each piston operates its own left/right lever, not a longitudinal
        # single-ended cylinder feeding a crossbar.
        x=cx+end*3.3
        bits.append(m.cylinder('横向き両ピストン式の内側シリンダ',(x,-2,9.25),(0,1,0),.95,4,g))
        for side in [-1,1]:
            bits.append(m.cylinder('シリンダ両端のフランジ',(x,side*1.9-.15,9.25),(0,1,0),1.18,.3,g))
            bits.append(m.tube('左右独立のシリンダ押棒',(x,side*1.9,9.25),(x,side*3.9,9.25),.24,g))
        for side in [-1,1]:
            dc=side*4.2;xx=cx+end*10.925
            for offset in [-.61,.61]:
                bits.append(m.box('ディスク両面のブレーキパッド',xx-.45,dc+offset-.14,7.4,.9,.28,.95,g))
                bits.append(m.box('内側キャリパー腕',xx-.55,dc+offset-.23,8.2,1.1,.46,.9,g))
            bits += [m.tube('パッドを挟む上部連結ピン',(xx,dc-.93,8.9),(xx,dc+.93,8.9),.27,g),
                m.tube('左右独立てことキャリパーの連結',(x,side*3.8,9.25),(xx,dc,8.9),.25,g)]
    # One internal supply line, attached at the upper bolster; no ground power cable.
    for a,b in [((cx-3.3,0,9.25),(cx-1.8,1.2,11.35)),
                ((cx-1.8,1.2,11.35),(cx+1.8,1.2,11.35)),
                ((cx+1.8,1.2,11.35),(cx+3.3,0,9.25))]:
        bits.append(m.tube('台車内のブレーキ供給管',a,b,.15,g))
    return bits


def disc(m,g,x,side):
    """Two friction faces with a ventilated middle. Keep lateral HO adaptation."""
    y=side*4.2;z=5.375
    blank=m.cylinder('二面式ブレーキディスク',(x,y-.36,z),(0,1,0),3.4,.72,g)
    slots=[]
    for j in range(12):
        a=2*math.pi*j/12+.07;b=2*math.pi*(j+1)/12-.07
        def p(r,t):return(x+r*math.cos(t),y-.16,z+r*math.sin(t))
        slots.append(m.wire('ディスク内部の放射状通気孔',[
            m.line(p(1.65,a),p(3.5,a)),m.arc((x,y-.16,z),(0,-1,0),(1,0,0),3.5,a,b-a),
            m.line(p(3.5,b),p(1.65,b)),m.arc((x,y-.16,z),(0,-1,0),(1,0,0),1.65,b,a-b)],g))
    blank=m.cut('二枚の摩擦面と放射状の内部リブ',blank,slots,(0,1,0),.32,g,'金属銀')
    hub=m.cylinder('ディスクの厚い取付ハブ',(x,y-.48,z),(0,1,0),1.65,.96,g)
    return m.joined('通気孔・内側ハブ付きディスク',[blank,hub],g,'金属銀')


def plow(m,g,cx):
    upper=m.rounded('プロウの丸い上板',(cx-20.5,-13.6,5.1),(0,1,0),(0,0,1),27.2,1.5,.35,g)
    upper=m.extrude('プロウの上板',[upper],(1,0,0),.45,g)
    lower=m.poly('プロウの折り返し下板',[(cx-20.45,-13.2,5.3),(cx-20.45,13.2,5.3),
        (cx-19.65,12.8,3.8),(cx-19.65,-12.8,3.8)],g)
    lower=m.extrude('プロウの斜めの下板',[lower],(1,0,0),.45,g)
    stays=[m.box('プロウ支持腕',cx-20.1,yy,6.2,10.1,1.0,.6,g) for yy in [-6.5,5.5]]
    return m.joined('前台車プロウ・折返し・支持腕',[upper,lower]+stays,g,'黒')


def detail_checks(m,cx):
    frame=m.keyparts['bogies'][0]
    def check(entity,label,p,inside):m.points.append(dict(entity=entity,label=label,point=list(p),expectedInside=inside))
    for end in [-1,1]:
        check(frame,'横向きシリンダの中央下側',(cx+end*3.3,0,8.6),True)
        check(frame,'縦向きシリンダを置かない空間',(cx+end*6,0,8.5),False)
        for side in [-1,1]:
            check(frame,'側枠の連続した平らな上縁',(cx+end*8,side*11.5,11.3),True)
            check(frame,'軸箱上の浅い独立開口',(cx+end*13.125,side*11.5,9.6),False)
            for offset in [-.61,.61]:
                check(frame,'ディスクの両面を挟むパッド',(cx+end*10.925,side*4.2+offset,7.8),True)
    for wheel,dx in zip(m.keyparts['wheelsets'],[-13.125,13.125]):
        for side in [-1,1]:
            a=math.pi/12;x=cx+dx+2.5*math.cos(a);z=5.375+2.5*math.sin(a);y=side*4.2
            check(wheel,'ディスク中央の放射状通気孔',(x,y,z),False)
            check(wheel,'ディスクの二面式摩擦板',(x,y+.27,z),True)
            check(wheel,'ディスク内部の放射状リブ',(cx+dx+2.5,y,5.375),True)
