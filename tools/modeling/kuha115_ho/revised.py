"""Revised 1/80 Kuha native model. All local dimensions remain explicit estimates."""
from pathlib import Path
import argparse, copy, hashlib, json, math, sys, uuid
sys.path.insert(0, str(Path(__file__).resolve().parent))
from build import Train, vec, expr

NS = uuid.UUID('174cd80a-e073-4ff4-9000-115122208024')
L, SIDE, WALL, FLOOR = 243.75, 18.125, 1.0, 15.3125
EAVE, CROWN, JOINT = 39.6, 45.675, 39.0
DOORS = [51.25, 126.25, 201.25]
PIVOTS = [35.625, 208.125]
GW = dict(width=7.2, height=5.8, bottom=31.45, radius=.8, rubber=.34)
BASE_WINDOWS = [(37.1,9,'fixed')]+[(x,w,t) for shift in [0,75] for x,w,t in
    [(65.8+shift,8.7,'fixed'),(80.5+shift,13.525,'sash'),
     (99.125+shift,13.525,'sash'),(113.75+shift,8.7,'fixed')]]+[(215.8,8.7,'fixed'),(230.5,13.525,'sash')]


class Revised(Train):
    def id(self):
        self.i += 1
        return str(uuid.uuid5(self.namespace,str(self.i)))

    def __init__(self,detailed=False):
        self.detailed=detailed
        self.namespace=uuid.UUID('bb7e684d-52c6-4d90-9525-115122208025') if detailed else NS
        super().__init__()
        self.d['documentId']=str(uuid.uuid5(self.namespace,'revised-main'))
        self.d['metadata'].update(title='クハ115 長野色 1/80｜実車調査反映・横長貫通扉窓',
            description='New native source features. Kuha115-1222/N15 pre-training Nagano appearance study; local radii, window positions and equipment dimensions photo-estimated, not a measured reconstruction. Gangway window is wider than high per owner correction.')
        self.assembly=[]; self.windows={}; self.layout={};self.keyparts={}
        if detailed:self.d['metadata']['title']='クハ115 長野色 1/80｜前面・屋根機器・台車の写真照合改訂'

    def plan(self,z,inset=0,shrink=0):
        y=SIDE-inset-shrink; r=3.8-inset; half=5.3
        angle=math.radians(8); slope=math.tan(angle)
        cy=y-r; yt=cy+r*math.sin(angle)
        cx=inset+r*math.cos(angle)+slope*(yt-half)
        tx=cx-r*math.cos(angle); x0=inset
        # Constant shallow fold, tangent quarter corners, ordinary rear end.
        return [self.line((x0,-half,z),(x0,half,z)), self.line((x0,half,z),(tx,yt,z)),
            self.arc((cx,cy,z),(0,0,1),(1,0,0),r,math.pi-angle,-math.pi/2+angle),
            self.line((cx,y,z),(L-inset,y,z)),self.line((L-inset,y,z),(L-inset,-y,z)),
            self.line((L-inset,-y,z),(cx,-y,z)),
            self.arc((cx,-cy,z),(0,0,1),(1,0,0),r,-math.pi/2,-math.pi/2+angle),
            self.line((tx,-yt,z),(x0,-half,z))]

    def shell(self,name,g,outside,inside,low,high):
        o=self.wire(name+'外輪郭',self.plan(low,outside),g)
        q=self.wire(name+'内輪郭',self.plan(low,inside),g)
        return self.extrude(name,[o,q],(0,0,1),high-low,g)

    def joined(self,name,parts,g,color='フォギーグレー',note=''):
        if len(parts)==1:return self.finish(parts[0],color,notes=note)
        return self.finish(self.boolean(name,parts[0],parts[1:],0,g,color),color,notes=note)

    def clear(self,first,second,label):
        self.checks.append(dict(first=first,second=second,label=label,noOverlap=True))

    def ribbed(self,name,x,y,z,r,height,turns,g):
        # Fuse each spring before combining the bogie; keep Boolean groups small.
        parts=[self.cylinder(name+'芯',(x,y,z),(0,0,1),r*.88,height,g)]
        pitch=height/turns
        for j in range(turns):
            parts.append(self.cylinder(name+'巻線',(x,y,z+pitch*(j+.18)),(0,0,1),r,pitch*.36,g))
        return self.joined(name,parts,g,'黒')


def window_layout():
    driver=[dict(x=x,w=w,type=t,z=27.3,h=10.45) for x,w,t in BASE_WINDOWS]
    assistant=[dict(x=29.5,w=6.25,type='sash',z=27.3,h=10.45),
               dict(x=37.5,w=5.25,type='fixed',z=27.3,h=10.45)]+copy.deepcopy(driver[1:-1])+[
               dict(x=226,w=5.8,type='sash',z=27.3,h=10.45),
               dict(x=236,w=4.5,type='toilet',z=30.0,h=6.7)]
    return {-1:driver,1:assistant}


def body(m,root):
    g=m.group('01 車体｜折妻・裾絞り・実開口',root)
    def hull(inset,low,high):
        bottom=m.wire('裾の水平断面',m.plan(low,inset,1.1),g)
        waist=m.wire('腰の水平断面',m.plan(24.0,inset),g)
        lower=m.loft('裾絞りの二断面',[bottom,waist],g)
        upper=m.extrude('窓面の一定断面',[waist],(0,0,1),high-24,g)
        return m.joined('外板' if inset==0 else '内側空間',[lower,upper],g)
    a=m.boolean('中空車体・裾絞り',hull(0,13.8,JOINT),[hull(WALL,13.6,JOINT+.15)],1,g)
    m.layout=window_layout()
    for sign,label in [(-1,'運転士側'),(1,'助手側')]:
        profiles=[]
        for j,w in enumerate(m.layout[sign]):
            profiles.append(m.rounded(label+f'窓開口{j+1}',(w['x']-w['w']/2,sign*19.5,w['z']),
                (1,0,0),(0,0,1),w['w'],w['h'],.65 if w['type']!='sash' else .28,g))
        for x in DOORS:profiles.append(m.rounded(label+'客扉開口',(x-8.125,sign*19.5,FLOOR),
                (1,0,0),(0,0,1),16.25,22.8,.35,g))
        profiles += [m.rounded(label+'独立運転室側窓',(10.0,sign*19.5,30.4),(1,0,0),(0,0,1),4.1,7.6,.45,g),
                     m.rounded(label+'乗務員扉',(17.0,sign*19.5,FLOOR),(1,0,0),(0,0,1),6.5,22.8,.35,g)]
        a=m.cut(label+'側面の実開口',a,profiles,(0,-sign,0),4.2,g)
    for side in [-1,1]:
        p=m.rounded('パノラミック開口',(-3,5.3 if side>0 else -18.4,30.6 if m.detailed else 29.35),
                    (0,1,0),(0,0,1),13.1,7.6 if m.detailed else 9.05,.8,g)
        a=m.cut('前面と回込窓を抜く'+str(side),a,[p],(1,0,0),11.4,g)
    p=m.rounded('前面貫通開口',(-2,-4.8,FLOOR),(0,1,0),(0,0,1),9.6,23.2,.65,g)
    a=m.cut('前面貫通開口を抜く',a,[p],(1,0,0),4.2,g)
    p=m.rounded('後妻貫通開口',(L+1,-4.45,FLOOR),(0,1,0),(0,0,1),8.9,23.0,.6,g)
    a=m.cut('後妻貫通開口を抜く',a,[p],(-1,0,0),3.5,g)
    m.finished['body']=m.finish(a,thickness=1,notes='裾絞り1.1mm、折妻8度、隅R3.8mmは写真による推定設計値。実測値ではない。')
    if m.detailed:
        for side in [-1,1]:
            for z,inside in [(30.2,True),(31.0,False),(38.5,True)]:
                for depth in [.3,.6,.9]:
                    m.points.append(dict(entity=a,label='短縮した前面窓と上下の外板',
                        point=[math.tan(math.radians(8))*4.7+depth,side*10,z],expectedInside=inside))


def side_details(m,root):
    frames=m.group('03 窓枠｜黒Hゴム・銀色二段サッシ',root)
    glasses=m.group('04 透明板｜前面・側面・扉',root)
    doors=m.group('05 扉・手すり・雨樋',root)
    m.keyparts['frames']=[];m.keyparts['glasses']=[];m.keyparts['doors']=[]
    for sign,label in [(-1,'運転士側'),(1,'助手側')]:
        for j,w in enumerate(m.layout[sign]):
            x,ww,z,h=w['x'],w['w'],w['z'],w['h'];sash=w['type']=='sash'
            r=.28 if sash else .65;pos=sign*(SIDE+.38)
            o=m.rounded('窓枠外',(x-ww/2-.13,pos,z-.13),(1,0,0),(0,0,1),ww+.26,h+.26,r+.13,frames)
            q=m.rounded('窓枠内',(x-ww/2+.25,pos,z+.25),(1,0,0),(0,0,1),ww-.5,h-.5,max(.12,r-.25),frames)
            a=m.extrude(label+f'窓{j+1} '+('二段サッシ' if sash else '固定Hゴム'),[o,q],(0,-sign,0),.35,frames)
            if sash:
                rail=m.box('上下サッシ中桟',x-ww/2+.15,min(pos,pos-sign*.35),z+h*.49,ww-.3,.35,.4,frames)
                a=m.boolean(label+f'窓{j+1} 中桟一体',a,[rail],0,frames,'アルミ銀')
            m.finish(a,'アルミ銀' if sash else '黒',.35);m.keyparts['frames'].append(a)
            p=m.rounded('客室透明板',(x-ww/2-.2,sign*(SIDE-1.12),z-.2),(1,0,0),(0,0,1),ww+.4,h+.4,r+.2,glasses)
            pane=m.finish(m.extrude(label+f'透明板{j+1}',[p],(0,-sign,0),.4,glasses),'透明',.4)
            m.keyparts['glasses'].append(pane);m.clear(m.finished['body'],a,'外板と窓枠');m.clear(m.finished['body'],pane,'外板と透明板')
            for dz in [.3,h/2,h-.3]:
                for depth in [.15,.65,.95]:m.points.append(dict(entity=m.finished['body'],label=label+f'窓{j+1}開口',
                    point=[x,sign*(SIDE-depth),z+dz],expectedInside=False))
        # One independent cab side window per side, not two.
        for x,z,ww,h,name in [(10,30.4,4.1,7.6,'独立運転室窓'),(18.0,28.8,4.5,8.0,'乗務員扉窓')]:
            pos=sign*(SIDE+.25 if name=='独立運転室窓' else SIDE-.01)
            o=m.rounded(name+'枠外',(x-.15,pos,z-.15),(1,0,0),(0,0,1),ww+.3,h+.3,.6,frames)
            q=m.rounded(name+'枠内',(x+.18,pos,z+.18),(1,0,0),(0,0,1),ww-.36,h-.36,.27,frames)
            frame=m.finish(m.extrude(label+name+'枠',[o,q],(0,-sign,0),.23,frames),'アルミ銀' if name=='独立運転室窓' else '黒',.23)
            m.keyparts['frames'].append(frame)
            m.clear(m.finished['body'],frame,'運転室窓枠と車体')
            pos=sign*(SIDE-1.12)
            p=m.rounded(name+'透明板',(x-.18,pos,z-.18),(1,0,0),(0,0,1),ww+.36,h+.36,.6,glasses)
            pane=m.finish(m.extrude(label+name+'透明板',[p],(0,-sign,0),.4,glasses),'透明',.4)
            m.keyparts['glasses'].append(pane)
        for index,cx in enumerate(DOORS):
            for half in [-1,1]:
                x=cx-7.98 if half<0 else cx+.08;pos=sign*(SIDE-.28)
                o=m.rounded('扉葉',(x,pos,FLOOR+.12),(1,0,0),(0,0,1),7.90,22.5,.3,doors)
                q=m.rounded('扉窓',(x+1.25,pos,27.7),(1,0,0),(0,0,1),5.4,8.9,.8,doors)
                leaf=m.finish(m.extrude(label+f'客扉{index+1}葉{half}',[o,q],(0,-sign,0),.55,doors),thickness=.55)
                m.keyparts['doors'].append(leaf);m.clear(m.finished['body'],leaf,'客扉と車体')
                o=m.rounded('客扉ゴム外',(x+1.1,sign*(SIDE-.01),27.55),(1,0,0),(0,0,1),5.7,9.2,.95,doors)
                q=m.rounded('客扉ゴム内',(x+1.48,sign*(SIDE-.01),27.93),(1,0,0),(0,0,1),4.94,8.44,.57,doors)
                rubber=m.finish(m.extrude('客扉Hゴム',[o,q],(0,-sign,0),.22,doors),'黒',.22)
                m.clear(leaf,rubber,'客扉と窓Hゴム')
                p=m.rounded('客扉裏ガラス',(x+1.1,sign*(SIDE-1.13),27.55),(1,0,0),(0,0,1),5.7,9.2,.95,glasses)
                pane=m.finish(m.extrude('客扉透明板',[p],(0,-sign,0),.4,glasses),'透明',.4);m.keyparts['glasses'].append(pane)
                handle=m.ring('半自動手掛け',(x+(6.9 if half<0 else 1),sign*(SIDE-.1),25.5),(0,sign,0),.30,.16,.15,doors)
                m.finish(handle,'アルミ銀',.15)
            m.finish(m.box('客扉踏板',cx-8.1,-SIDE-.10 if sign<0 else SIDE-.65,FLOOR-.15,16.2,.75,.18,doors),'アルミ銀',.18)
        pos=sign*(SIDE-.28)
        o=m.rounded('乗務員扉葉',(17.12,pos,FLOOR+.12),(1,0,0),(0,0,1),6.26,22.5,.3,doors)
        q=m.rounded('乗務員扉ガラス開口',(18.0,pos,28.8),(1,0,0),(0,0,1),4.5,8.0,.5,doors)
        leaf=m.finish(m.extrude(label+'乗務員扉',[o,q],(0,-sign,0),.55,doors),thickness=.55)
        details=[m.box('乗務員ヒンジ',17.13,min(pos,pos+sign*.25),z,.4,.25,.8,doors) for z in [19,27,35]]
        details.append(m.box('乗務員取手',22.7,min(pos,pos+sign*.25),23.0,.35,.25,1.0,doors))
        leaf=m.joined(label+'乗務員扉ヒンジ・取手',details+[leaf],doors)
        m.clear(m.finished['body'],leaf,'乗務員扉と外板');m.keyparts['doors'].append(leaf)
        for x in [16.1,24.1]:
            bars=[m.tube('乗務員手すり',(x,sign*(SIDE+.45),20.0),(x,sign*(SIDE+.45),29.2),.17,doors)]
            bars += [m.tube('手すり足',(x,sign*(SIDE+.02),z),(x,sign*(SIDE+.45),z),.17,doors) for z in [20,29.2]]
            m.joined(label+'乗務員手すり',bars,doors,'アルミ銀')
        # Max envelope 36.875 mm includes these gutters.
        gutter=m.finish(m.box(label+'雨樋',8,min(sign*18.4375,sign*18.125),38.7,L-8,.3125,.35,doors),thickness=.35)
        m.finished['gutter'+str(sign)]=gutter


def front(m,root):
    g=m.group('06 前頭部｜横長貫通扉窓・折妻・縦桟',root)
    bottom=30.38 if m.detailed else 29.13; height=8.04 if m.detailed else 9.49
    for side in [-1,1]:
        y=5.12 if side>0 else -18.55
        shell=m.shell('前面Hゴム素材',g,-.32,-.03,bottom,bottom+height)
        o=m.rounded('ゴム外マスク',(-3,y,bottom),(0,1,0),(0,0,1),13.43,height,1.0,g)
        q=m.rounded('ゴム内マスク',(-3,y+.4,bottom+.4),(0,1,0),(0,0,1),12.63,height-.8,.6,g)
        tool=m.extrude('前面窓ゴム切出工具',[o,q],(1,0,0),11.4,g)
        rim=m.boolean('前面窓Hゴム'+str(side),shell,[tool],2,g,'黒')
        cap=m.box('回込窓端の縦ゴム',8.05,min(side*(SIDE+.32),side*(SIDE+.03)),bottom+.2,.35,.29,height-.39,g)
        rim=m.joined('パノラミック窓ゴム一体'+str(side),[rim,cap],g,'黒')
        m.clear(m.finished['body'],rim,'パノラミックゴムと車体')
        shell=m.shell('前面透明板素材',g,.22 if m.detailed else 1.08,.62 if m.detailed else 1.48,bottom+.27,bottom+height-.27)
        p=m.rounded('前面ガラスマスク',(-3,5.35 if side>0 else -18.2,bottom+.27),(0,1,0),(0,0,1),12.85,height-.54,.8,g)
        tool=m.extrude('透明板切出工具',[p],(1,0,0),11.0,g)
        pane=m.boolean('前面曲面透明板'+str(side),shell,[tool],2,g,'透明')
        m.keyparts['glasses'].append(pane);m.clear(m.finished['body'],pane,'前面透明板と車体')
        # Divider lies on the flat/curved transition rather than bisecting the window.
        yy=14.6*side;xx=math.tan(math.radians(8))*(14.6-5.3)-.14
        divider=m.tube('前面窓の銀色縦桟',(xx,yy,bottom+.37),(xx,yy,bottom+height-.32),.13,g)
        divider=m.boolean('縦桟の両端を窓ゴムに合わせる',divider,[rim],1,g,'アルミ銀')
        m.entities[rim]['visibility']='visible'
        if m.detailed:
            from detail_parts import wiper
            wiper(m,g,side)
            continue
        arm=[m.tube('ワイパー腕',(-.40,side*8,29.8),(-.48,side*10.4,34.5),.15,g),
             m.tube('ワイパー連動腕',(-.48,side*8.5,29.8),(-.56,side*10.75,34.5),.12,g),
             m.tube('ワイパーブレード',(-.56,side*10.2,34.2),(-.56,side*12.2,36.3),.14,g)]
        m.joined('ワイパー腕・連動腕・ブレード一体',arm,g,'黒')
    # Common source dimensions for opening, rubber and glass. Width/height > 1.
    o=m.rounded('前面貫通扉輪郭',(-.20,-4.65,FLOOR+.12),(0,1,0),(0,0,1),9.3,22.9,.55,g)
    def gw(name,x,pad):
        return m.rounded(name,(x,-GW['width']/2-pad,GW['bottom']-pad),(0,1,0),(0,0,1),
            GW['width']+2*pad,GW['height']+2*pad,GW['radius']+pad,g)
    q=gw('横長貫通扉窓開口',-.20,0)
    door=m.finish(m.extrude('貫通扉',[o,q],(1,0,0),.70,g),thickness=.70)
    rim=m.finish(m.extrude('横長貫通扉窓Hゴム',[gw('Hゴム外',-.52,.20),gw('Hゴム内',-.52,-.16)],(1,0,0),.26,g),'黒',.26)
    pane=m.finish(m.extrude('横長貫通扉窓透明板',[gw('貫通扉裏透明板',.64,.22)],(1,0,0),.40,g),'透明',.40)
    m.keyparts['gangway']=[door,rim,pane];m.keyparts['glasses'].append(pane)
    m.clear(m.finished['body'],door,'前面貫通扉と車体');m.clear(door,pane,'貫通扉と透明板')
    for y in [-2.8,0,2.8]:
        for z in [GW['bottom']+.5,GW['bottom']+GW['height']/2,GW['bottom']+GW['height']-.5]:
            m.points.append(dict(entity=door,label='横長貫通扉窓の開口',point=[.1,y,z],expectedInside=False))
    for z in [30.8,37.8]:m.points.append(dict(entity=door,label='貫通扉窓の上下に扉板が残る',point=[.1,0,z],expectedInside=True))
    o=m.rounded('幌受外',(-.70,-5.12,FLOOR-.2),(0,1,0),(0,0,1),10.24,23.65,.9,g)
    q=m.rounded('幌受内',(-.70,-4.72,FLOOR+.2),(0,1,0),(0,0,1),9.44,22.85,.5,g)
    rim=m.finish(m.extrude('幌受け',[o,q],(1,0,0),.40,g),thickness=.4)
    ears=[m.ring('幌受上の締結耳',(-.9,y,39.0),(1,0,0),.4,.18,.4,g) for y in [-3.0,3.0]]
    m.joined('幌受け・締結耳一体',[rim]+ears,g)
    m.finish(m.box('貫通扉取手',-.56,3.6,27.2,.35,.65,.3,g),'アルミ銀')
    if m.detailed:
        from detail_parts import gangway_fittings
        gangway_fittings(m,g)
    else:m.finish(m.box('下部渡り板',-.95,-4.2,FLOOR+.3,.70,8.4,2.4,g),thickness=.70)
    for side in [-1,1]:
        for name,y,z,r,color in [('前照灯',12.7,25.7,1.52,'透明'),('尾灯',12.7,20.2,.80,'赤透明')]:
            if m.detailed:
                from detail_parts import lamp
                lamp(m,g,side,name,y,z,r,color)
                continue
            xx=math.tan(math.radians(8))*(y-5.3)
            n=(-math.cos(math.radians(8)),side*math.sin(math.radians(8)),0)
            center=(xx-.70,side*y,z)
            m.finish(m.ring(name+'灯具',center,n,r,r-.23,.55,g),'アルミ銀' if name=='前照灯' else '床下グレー')
            m.finish(m.cylinder(name+'レンズ',(center[0]-.05,center[1],z),n,r-.27,.15,g),color)
        y=9.5;xx=math.tan(math.radians(8))*(y-5.3)
        if m.detailed:
            n=(-math.cos(math.radians(8)),side*math.sin(math.radians(8)),0)
            def at(d,z):return (xx+n[0]*d,side*y+n[1]*d,z)
            cover=m.cylinder('前面の折角に沿うタイフォン丸胴',at(-.05,25.7),n,1.23,.95,g)
            upper=m.box('シャッター上部覆い',xx-1.0,side*y-.7,26.55,1.05,1.4,.7,g)
            cover=m.joined('奥行きのある丸形タイフォン覆い',[cover,upper],g,'アドバンスブルー')
            cover=m.boolean('タイフォン背面を外板に合わせる',cover,[m.finished['body']],1,g,'アドバンスブルー')
            m.entities[m.finished['body']]['visibility']='visible'
            line=m.tube('シャッター中央線',at(.94,24.55),at(.94,26.8),.045,g)
            m.boolean('シャッター線の食込みを除く',line,[cover],1,g,'黒')
            m.entities[cover]['visibility']='visible'
        else:
            cover=m.cylinder('タイフォン丸形シャッター',(xx-.42,side*y,25.7),(-1,0,0),1.23,.30,g)
            upper=m.box('シャッター上部覆い',xx-.58,side*y-.7,26.55,.40,1.4,.7,g)
            m.joined('丸形タイフォン覆い',[cover,upper],g,'アドバンスブルー')
            m.finish(m.tube('シャッター中央線',(xx-.79,side*y,24.55),(xx-.79,side*y,26.8),.045,g),'黒')
        for z,w in [(28.55,6.8),(21.4,1.7)]:
            cy=side*(10.7 if w>3 else 9.7)
            m.finish(m.tube('前面横手掛け',(-.14,cy-w/2,z),(-.14,cy+w/2,z),.13,g),'アドバンスブルー' if z>26 else 'フォギーグレー')
        m.finish(m.box('前面裾ステップ',.0,side*12.3-2.7,17.0,.60,5.4,.2,g),'アルミ銀')
    # Rear door deliberately uses its own, unconfirmed prototype shape.
    rg=m.group('後妻｜貫通扉・渡り板',g)
    o=m.rounded('後妻扉輪郭',(L+.1,-4.3,FLOOR+.12),(0,1,0),(0,0,1),8.6,22.7,.5,rg)
    q=m.rounded('後妻扉窓',(L+.1,-3,30),(0,1,0),(0,0,1),6,6.8,.7,rg)
    rear=m.finish(m.extrude('後妻貫通扉',[o,q],(-1,0,0),.70,rg),thickness=.7)
    m.clear(m.finished['body'],rear,'後妻扉と車体')
    p=m.rounded('後妻透明板',(L-.75,-3.2,29.8),(0,1,0),(0,0,1),6.4,7.2,.9,rg)
    m.finish(m.extrude('後妻透明板',[p],(-1,0,0),.4,rg),'透明',.4)
    o=m.rounded('後妻幌外',(L+.5,-4.8,FLOOR-.2),(0,1,0),(0,0,1),9.6,23.4,.8,rg)
    q=m.rounded('後妻幌内',(L+.5,-4.4,FLOOR+.2),(0,1,0),(0,0,1),8.8,22.6,.4,rg)
    m.finish(m.extrude('後妻幌受け',[o,q],(-1,0,0),.35,rg),'床下グレー')


def roof_profile(m,g,x,top,inset=0,hollow=False,shift=0):
    """Tangent central/shoulder arcs; concentric normal offset for inner profile."""
    rt=40.0;h=top-EAVE;rs=(2*rt*h-SIDE*SIDE-h*h)/(2*(rt-SIDE))
    cy=SIDE-rs;zc=top-rt;theta=math.atan2(EAVE-zc,cy)
    zc+=shift
    r1,r2=rt-inset,rs-inset
    if r2<=0:raise ValueError('roof shoulder offset is too thick')
    def pt(center,r,a):return (x,center[0]+r*math.cos(a),center[1]+r*math.sin(a))
    right=(cy,EAVE+shift);left=(-cy,EAVE+shift);mid=(0,zc)
    p0=pt(right,r2,0);p1=pt(left,r2,math.pi)
    bottom=(JOINT if inset==0 else JOINT-.2)+shift
    if hollow:
        a=.85;ri=rt-a;si=rs-a
        if si<=0:raise ValueError('Invalid inner shoulder')
        il=pt(left,si,math.pi);ir=pt(right,si,0)
        return m.wire('屋根の中空閉断面',[
            m.arc((x,*right),(1,0,0),(0,1,0),rs,0,theta),
            m.arc((x,*mid),(1,0,0),(0,1,0),rt,theta,math.pi-2*theta),
            m.arc((x,*left),(1,0,0),(0,1,0),rs,math.pi-theta,theta),
            m.line(p1,(x,p1[1],JOINT)),m.line((x,p1[1],JOINT),(x,il[1],JOINT)),m.line((x,il[1],JOINT),il),
            m.arc((x,*left),(1,0,0),(0,1,0),si,math.pi,-theta),
            m.arc((x,*mid),(1,0,0),(0,1,0),ri,math.pi-theta,-math.pi+2*theta),
            m.arc((x,*right),(1,0,0),(0,1,0),si,theta,-theta),
            m.line(ir,(x,ir[1],JOINT)),m.line((x,ir[1],JOINT),(x,p0[1],JOINT)),m.line((x,p0[1],JOINT),p0)],g)
    return m.wire('屋根接線連続断面',[
        m.line((x,p0[1],bottom),p0),m.arc((x,*right),(1,0,0),(0,1,0),r2,0,theta),
        m.arc((x,*mid),(1,0,0),(0,1,0),r1,theta,math.pi-2*theta),
        m.arc((x,*left),(1,0,0),(0,1,0),r2,math.pi-theta,theta),
        m.line(p1,(x,p1[1],bottom)),m.line((x,p1[1],bottom),(x,p0[1],bottom))],g)


def roof(m,root):
    g=m.group('02 屋根｜前頭ドーム・肩の接線連続',root)
    # A full-width loft is trimmed by the folded body plan, avoiding a pinched nose.
    stations=[(-.05,44.7),(1.5,44.9),(3.5,45.3),(6,45.6),(8.5,CROWN)]
    def spans(name,hollow):
        profiles=[roof_profile(m,g,x,z,hollow=hollow) for x,z in stations]
        parts=[m.loft(name+'前頭区間'+str(i),[a,b],g) for i,(a,b) in enumerate(zip(profiles,profiles[1:]))]
        parts.append(m.extrude(name+'一般部',[profiles[-1]],(1,0,0),L-8.5,g))
        return m.joined(name,parts,g,'屋根グレー')
    if m.detailed:
        from front_roof import Forehead
        spec=Forehead(L,SIDE,EAVE,CROWN,JOINT)
        outside,a=spec.build(m,g)
    else:
        outside=spans('屋根外形',False)
        ring=spans('屋根中空断面の連続ロフト',True)
        o=m.wire('屋根の平面外形',m.plan(38.7),g);clip=m.extrude('屋根平面トリム',[o],(0,0,1),12,g)
        a=m.boolean('前頭の平面形に合わせた中空屋根',ring,[clip],2,g,'屋根グレー')
        shell=m.shell('前頭の額素材',g,0,.9,JOINT,45.8)
        mask=m.box('前頭額の範囲',-1,-20,JOINT,9.5,40,8,g)
        shell=m.boolean('前頭額の外板',shell,[mask],2,g,'屋根グレー')
        shell=m.boolean('屋根曲面に沿う額',shell,[outside],2,g,'屋根グレー')
        a=m.joined('前頭屋根と一般屋根の中空一体',[a,shell],g,'屋根グレー')
    m.finished['roof']=a;m.clear(m.finished['body'],a,'車体と屋根')
    if m.detailed:m.mesh_chords[a]=.006
    if m.detailed:
        spec.checks(m,a)
    # Separate pale forehead skin, a part of the closed roof, cut at X=8.5.
    # Paint masks handle its color in preview; geometry remains one roof part.
    if m.detailed:
        from detail_parts import destination
        destination(m,g,outside)
    else:
        o=m.rounded('前面表示枠外',(-.3,-4.65,40.2),(0,1,0),(0,0,1),9.3,2.4,.5,g)
        q=m.rounded('前面表示枠内',(-.3,-4.25,40.55),(0,1,0),(0,0,1),8.5,1.7,.25,g)
        m.finish(m.extrude('前面表示器枠',[o,q],(-1,0,0),.35,g),thickness=.35)
        p=m.rounded('前面表示面',(-.10,-4.25,40.55),(0,1,0),(0,0,1),8.5,1.7,.25,g)
        m.finish(m.extrude('前面表示面',[p],(-1,0,0),.15,g),'黒')
    eg=m.group('07 屋根機器｜AU75G・通風器・歩み板',root)
    x0=L/2-45.75/2;z0=CROWN+.40
    if m.detailed:
        from detail_parts import climate_unit
        ac=climate_unit(m,eg,x0,z0)
    else:
        p=m.poly('AU75G横断外形',[(x0,-13.1875,z0),(x0+45.75,-13.1875,z0),
        (x0+45.75,-13.1875,z0+2.9),(x0+43,-13.1875,z0+4.7),
        (x0+2.75,-13.1875,z0+4.7),(x0,-13.1875,z0+2.9)],eg)
        ac=m.extrude('AU75Gキセ',[p],(0,1,0),26.375,eg)
    holes=[m.circle('AU75Gファン開口',(x,0,z0+3.8),(0,0,1),3.6,eg) for x in [x0+8.5,x0+37.25]]
    ac=m.cut('AU75G二つのファン',ac,holes,(0,0,1),1.2,eg,'アルミ銀')
    details=[ac]
    for x in [x0+8.5,x0+37.25]:
        details.append(m.ring('ファン保護環',(x,0,z0+4.5),(0,0,1),3.9,3.6,.2,eg))
        for dy in [-2.7,-1.8,-.9,0,.9,1.8,2.7]:
            half=math.sqrt(3.8**2-dy**2);details.append(m.box('ファン格子',x-half,dy-.08,z0+4.5,2*half,.16,.2,eg))
    if not m.detailed:
        for sign in [-1,1]:
            for x in [x0+12+j*1.1 for j in range(20)]:
                details.append(m.box('キセのルーバー',x,sign*13.1-.10,z0+.5,.22,.20,2.3,eg))
    ac=m.joined('AU75G ステンレスキセ・ファン格子',details,eg,'アルミ銀',note='製造者の外形寸法。ファン中心・格子・脚は写真推定。')
    # A thick foot spans to the curved roof; trim its bottom with actual roof solid.
    feet=[m.box('冷房装置の取付脚',x,y,44.8,3.6,1.0,1.4,eg) for x in [x0+4,x0+38] for y in [-7.5,6.5]]
    ac=m.joined('AU75G・脚一体',[ac]+feet,eg,'アルミ銀')
    ac=m.boolean('AU75G脚底を屋根に合わせる',ac,[outside],1,eg,'アルミ銀')
    if m.detailed:
        fans=[]
        for fan in m.keyparts['climateFans']:
            fan=m.boolean('ファン支持桟の端をキセに合わせる',fan,[ac],1,eg,'黒')
            fans.append(fan);m.clear(ac,fan,'キセとファン支持桟')
        m.keyparts['climateFans']=fans;m.entities[ac]['visibility']='visible'
    m.entities[a]['visibility']='visible';m.clear(a,ac,'屋根と冷房装置')
    if m.detailed:
        for y,inside in [(-12.5,False),(0,True),(12.5,False)]:
            m.points.append(dict(entity=ac,label='AU75Gの幅方向アーチを実形状で確認',
                point=[x0+22,y,z0+4.2],expectedInside=inside))
    for y in [-15.2,13.7]:
        plate=m.box('クーラー脇歩み板',x0+2,y,43.4,41.7,1.5,.3,eg)
        if m.detailed:
            legs=[m.box('歩み板の支持脚',x,y+.35,42.2,.65,.8,1.35,eg) for x in [x0+3,x0+15,x0+28,x0+42]]
            plate=m.joined('歩み板・四本の支持脚',[plate]+legs,eg,'屋根グレー')
            plate=m.boolean('屋根に接する脚付き歩み板',plate,[outside],1,eg,'屋根グレー')
            m.entities[a]['visibility']='visible';m.clear(a,plate,'屋根と脚付き歩み板')
        else:m.finish(plate,'屋根グレー',.3)
    # Rounded open-mouthed ventilators, not solid trapezoids.
    for x in [28.5,58.5,83,166.5,192,222.5]:
        if m.detailed:
            from detail_parts import ventilator
            vent=ventilator(m,eg,x)
        else:
            o=m.rounded('通風器外断面',(x-3.6,-3.1,45.15),(0,1,0),(0,0,1),6.2,2.1,.65,eg)
            q=m.rounded('通風器空洞',(x-3.6,-2.65,45.5),(0,1,0),(0,0,1),5.3,1.25,.35,eg)
            vent=m.extrude('通風器 外覆い',[o,q],(1,0,0),7.2,eg)
            ribs=[m.box('通風器の内部仕切',x-3.6,y,45.45,7.2,.22,1.35,eg) for y in [-1.8,0,1.8]]
            vent=m.joined('押込通風器 開口と仕切付き',[vent]+ribs,eg,'屋根グレー')
        # Remove overlap using the same evaluated roof as contact tool.
        vent=m.boolean('屋根曲率に合わせた通風器',vent,[outside],1,eg,'屋根グレー')
        m.clear(a,vent,'屋根と通風器');m.mesh_chords[vent]=.006
        m.entities[a]['visibility']='visible'
    if m.detailed:
        base=m.box('列車無線アンテナの角形架台',9.2,-1.8,46.05,4.6,3.6,.45,eg)
        feet=[m.box('アンテナ架台の支持脚',x,-1.5,45.3,.65,3.0,.85,eg) for x in [9.5,12.9]]
        aerial=m.cylinder('列車無線アンテナ',(11.25,0,46.4),(0,0,1),.47,2.2,eg)
        cable=[m.tube('アンテナ引込線',p,q,.10,eg) for p,q in zip(
            [(10.0,1.5,46.25),(8.6,1.5,45.95),(6.0,1.5,45.45)],
            [(8.6,1.5,45.95),(6.0,1.5,45.45),(5.7,1.5,45.0)])]
        aerial=m.joined('列車無線アンテナ・架台・脚・引込線',[base,aerial]+feet+cable,eg,'屋根グレー')
    else:
        base=m.box('列車無線アンテナ台座',10,-1.3,44.65,2.5,2.6,.6,eg)
        aerial=m.cylinder('列車無線アンテナ',(11.25,0,45.2),(0,0,1),.47,1.7,eg)
        aerial=m.joined('列車無線アンテナ',[base,aerial],eg,'屋根グレー')
    aerial=m.boolean('アンテナ台座の屋根合わせ',aerial,[outside],1,eg,'屋根グレー')
    m.entities[a]['visibility']='visible';m.clear(a,aerial,'屋根とアンテナ')
    if m.detailed:
        for y in [-10,10]:
            path=[(x,y,spec.height_at(x,y)+offset) for x,offset in [(2.4,-.3),(2.9,.65),(4.5,.65),(5.2,-.3)]]
            bits=[m.tube('前頭屋根ループ手掛け',p,q,.13,eg) for p,q in zip(path,path[1:])]
            rail=m.joined('前頭屋根手掛け・取付足',bits,eg,'屋根グレー')
            rail=m.boolean('屋根曲面に合わせたループ手掛け',rail,[outside],1,eg,'屋根グレー')
            m.entities[a]['visibility']='visible';m.clear(a,rail,'前頭屋根とループ手掛け')
    flare=m.cylinder('信号炎管',(4.8,-5.5,44.15),(0,0,1),.38,3.6 if m.detailed else 1.7,eg)
    if m.detailed:
        head=m.cylinder('炎管の傘状頭部',(4.8,-5.5,47.35),(0,0,1),.78,.40,eg)
        cap=m.cylinder('炎管頭部の上段',(4.8,-5.5,47.70),(0,0,1),.45,.20,eg)
        flare=m.joined('信号炎管・頭部一体',[flare,head,cap],eg,'屋根グレー')
    flare=m.boolean('炎管の屋根合わせ',flare,[outside],1,eg,'屋根グレー')
    m.entities[a]['visibility']='visible';m.clear(a,flare,'屋根と炎管')
    if m.detailed:m.points.append(dict(entity=flare,label='信号炎管の幅広い傘状頭部',point=[4.8,-6.1,47.55],expectedInside=True))


def chassis(m,root):
    fg=m.group('08 床板｜着脱式・台車中心穴',root)
    p=m.wire('床板外形',m.plan(13.65,2.2),fg)
    holes=[m.circle('台車取付穴',(x,0,13.65),(0,0,1),1.1,fg) for x in PIVOTS]
    floor=m.finish(m.extrude('床板',[p]+holes,(0,0,1),1.3,fg),'床下グレー',1.3)
    m.finished['floor']=floor;m.clear(m.finished['body'],floor,'外板と床板')
    g=m.group('09 床下｜写真配置・スカート・連結器',root)
    # These are explicit external appearance estimates, not an invented circuit diagram.
    for j,(x,y,w,d,h) in enumerate([(61,-14.6,13,7.5,5.3),(79,-14.6,20,7.5,5.8),
                (103,-14.6,22,7.5,5.4),(130,-14.6,16,7.5,5.8),(151,-14.6,20,7.5,4.7),
                (71,6.2,22,8,5.6),(103,6.2,16,8,4.9),(132,6.2,18,8,5.7)]):
        p=m.box('床下箱・外形推定'+str(j+1),x,y,13.5-h,w,d,h,g)
        ribs=[m.box('吊り金具',xx,y-.2,13.3,1,d+.4,.35,g) for xx in [x+1,x+w-2]]
        p=m.joined('床下箱'+str(j+1)+' 取付金具付き',[p]+ribs,g,'床下グレー',note='クハ1222写真からの外形配置案。機器同定と反対側位置に未確認あり。')
    for x,y,r,le,name in [(55,0,1.7,12,'空気溜外形案'),(172,5,2.8,13,'便所関連タンク外形案')]:
        p=m.cylinder(name,(x,y,10.6),(1,0,0),r,le,g)
        feet=[m.box('タンク吊り脚',xx,y-.65,12.0,1,1.3,1.65,g) for xx in [x+1,x+le-2]]
        m.joined(name+'・支持部',[p]+feet,g,'床下グレー',note='形と位置は写真推定、給排水管の完全再現ではない。')
    # Continuous skirt bottom underneath a central coupler aperture.
    if m.detailed:
        panels=[]
        for ya,yb,xa,xb in [(-17.1,-12.7,1.2,-.55),(-12.7,12.7,-.55,-.55),(12.7,17.1,-.55,1.2)]:
            ba,bb=max(-16,ya),min(16,yb)
            def bend(y):return xa+(xb-xa)*(y-ya)/(yb-ya)
            p=m.poly('スカートの左右回込板',[(bend(ba),ba,6.6),(bend(bb),bb,6.6),(xb,yb,13.7),(xa,ya,13.7)],g)
            panels.append(m.extrude('折り曲げたスカート板',[p],(1,0,0),.75,g))
        skirt=m.joined('左右へ折れる一体スカート',panels,g,'床下グレー')
        from detail_parts import skirt_mask
        mask=skirt_mask(m,g)
        skirt=m.boolean('スカート下隅の丸み',skirt,[mask],2,g,'床下グレー')
        gap=m.box('車体下と中央スカートの空間',-2,-12.7,10.7,5,25.4,5,g)
        skirt=m.boolean('中央の上縁を下げたスカート',skirt,[gap],1,g,'床下グレー')
    else:
        pts=[(-.55,-16.0,8.4),(-.55,16,8.4),(-.55,17.1,13.7),(-.55,-17.1,13.7)]
        o=m.poly('スカート外周',pts,g);skirt=m.extrude('スカート素材',[o],(1,0,0),.75,g)
    q=m.rounded('連結器切欠',(-2,-3.1,10.0),(0,1,0),(0,0,1),6.2,4.6,.5,g)
    skirt=m.cut('スカート 中央下縁連続',skirt,[q],(1,0,0),5,g,'床下グレー')
    m.finished['skirt']=skirt
    if m.detailed:
        for side in [-1,1]:
            m.points.extend([dict(entity=skirt,label='スカートの下絞り内側',point=[.85,side*15.4,7.0],expectedInside=True),
                             dict(entity=skirt,label='スカート下絞りの外側は空間',point=[1.2,side*16.7,7.0],expectedInside=False)])
            for y in [6,10]:
                m.points.append(dict(entity=skirt,label='ジャンパ栓の背後に開いたスカート上部',point=[-.3,side*y,12],expectedInside=False))
    for front,x in [(True,-3.125),(False,L+2.325)]:
        head=m.box('連結器頭',x,-1.7 if m.detailed else -1.35,10.35 if m.detailed else 10.6,
                   1.2 if m.detailed else .85,3.4 if m.detailed else 2.7,2.3 if m.detailed else 1.8,g)
        hole=m.circle('連結器口',(x-.1,0,11.45),(1,0,0),.55,g)
        head=m.cut('密着連結器の口',head,[hole],(1,0,0),1.5,g,'床下グレー')
        if m.detailed:
            jaw=m.box('連結器のかみ合い突起',x-.25,-1.7,10.35,.4,.8,1.15,g)
            horn=m.box('連結器の上部案内突起',x-.15,.9,12.1,.35,.8,.55,g)
            head=m.joined('密着連結器の段差付き頭部',[head,jaw,horn],g,'床下グレー')
        stem=m.box('連結器柄',-2.4 if front else L-4,-.55,10.95,6.5,1.1,1.0,g)
        mount=m.box('連結器支持台',2.8 if front else L-3.8,-1,11.4,1,2,2.25,g)
        m.joined('密着連結器・取付座',[head,stem,mount],g,'床下グレー')
    for y in [-7,-4.4,4.4,7,9.2]:
        socket=m.cylinder('前面ジャンパ栓',(-.8,y,12.0),(-1,0,0),.50,.5,g)
        pipe=m.tube('ジャンパ管',(-1.2,y,11.7),(-1.1,y+.7,9.7),.15,g)
        bits=[socket,pipe]
        if m.detailed:bits.append(m.tube('ジャンパ管のスカート取付足',(-1.1,y+.7,9.7),(-.54,y+.7,9.7),.18,g))
        jumper=m.joined('前面ジャンパ栓・管・取付足一体' if m.detailed else '前面ジャンパ栓・管一体',bits,g,'床下グレー')
        if m.detailed:
            m.boolean('取付足をスカート前面で合わせる',jumper,[skirt],1,g,'床下グレー')
            m.entities[skirt]['visibility']='visible'


def bogies(m,root):
    g0=m.group('10 台車｜TR62系構造・HO16.5機構',root)
    m.keyparts['bogies']=[];m.keyparts['wheelsets']=[]
    for cx,lab in zip(PIVOTS,['前','後']):
        g=m.group(lab+'台車',g0)
        # Two transoms, upper bolster, lower bolster, open outside sideframes.
        bits=[m.box('横梁',cx+dx,-11.4,9.2,1.2,22.8,1.5,g) for dx in [-3.7,2.5]]
        bits += [m.box('上揺れ枕',cx-2.6,-10.5,10.9,5.2,21,1.3,g),
                 m.box('下揺れ枕',cx-3.7,-13.1,6.6,7.4,26.2,.8,g),
                 m.cylinder('心皿',(cx,0,11.8),(0,0,1),2.2,1.7,g)]
        for side in [-1,1]:
            y=side*12.0;start=min(y,y-side*1.3)
            pts=[(cx-16,y,7.2),(cx-15,y,9.8),(cx-11,y,10.5),(cx-6,y,9.8),
                 (cx-4.4,y,10.2),(cx+4.4,y,10.2),(cx+6,y,9.8),(cx+11,y,10.5),
                 (cx+15,y,9.8),(cx+16,y,7.2),(cx+7,y,7.2),(cx+4.7,y,8.0),
                 (cx-4.7,y,8.0),(cx-7,y,7.2)]
            if m.detailed:
                from detail_parts import sideframe as curved_sideframe
                frame=curved_sideframe(m,g,cx,y,side)
            else:
                p=m.poly('側枠外周',pts,g)
                frame=m.extrude('開口付き側枠素材',[p],(0,-side,0),1.3,g)
                holes=[m.rounded('側枠開口',(cx+dx-2.2,y+side*.1,8.7),(1,0,0),(0,0,1),4.4,1.15,.4,g) for dx in [-8.5,8.5]]
                frame=m.cut('側枠の空間',frame,holes,(0,-side,0),1.6,g,'黒')
            bits.append(frame)
            for dx in [-13.125,13.125]:
                bits.append(m.box('軸箱支持枠',cx+dx-2.0,start-.2,4.5,4.0,1.7,3.2,g))
                bits.append(m.cylinder('軸箱丸蓋',(cx+dx,side*12.10,5.65),(0,side,0),1.35,.4,g))
                for off in [-2.45,2.45]:
                    bits.append(m.ribbed('軸ばね',cx+dx+off,side*11.5,6.5,1.05 if m.detailed else .75,2.3 if m.detailed else 2.0,4,g))
                if m.detailed:
                    for sign in [-1,1]:
                        bits.append(m.cylinder('軸箱蓋ボルト',(cx+dx+sign*.75,side*12.45,5.65),(0,side,0),.18,.18,g))
            # Two large bolster coils each side, with concentric ridge rings.
            for dx in [-2.0625,2.0625]:
                bits.append(m.ribbed('大径枕ばね',cx+dx,side*11.5,7.3,1.72,3.7,5,g))
            bits.append(m.cylinder('縦オイルダンパー',(cx,side*13.2,7.2),(0,0,1),.46,3.6,g))
            bits += [m.box('ダンパー取付耳',cx-.65,side*13.2-.5,z,1.3,1.0,.4,g) for z in [7.1,10.65]]
            for dx in [-3.6,3.6]:bits.append(m.tube('揺れ枕吊りリンク',(cx+dx,side*12.7,7.1),(cx+dx*.95,side*11.7,11.2),.25,g))
        def named(*names):return [b for b in bits if m.entities[b]['displayName'] in names]
        lower=named('下揺れ枕');coils=named('大径枕ばね');frames=named('側枠の空間')
        upper=named('横梁','上揺れ枕','心皿')
        used=set(lower+coils+frames+upper)
        a=m.joined(lab+'下揺れ枕と枕ばね',lower+coils,g,'黒')
        a=m.joined(lab+'左右側枠の接続',[a]+frames,g,'黒')
        a=m.joined(lab+'上揺れ枕と横梁',[a]+upper,g,'黒')
        rest=[b for b in bits if b not in used]
        for index,b in enumerate(rest):
            a=m.joined(lab+'台車 接続'+str(index)+' '+m.entities[b]['displayName'],[a,b],g,'黒')
        holes=[m.circle('車軸貫通穴',(cx+dx,-12.05 if m.detailed else -14,5.375),(0,1,0),.96,g) for dx in [-13.125,13.125]]
        a=m.cut(lab+'台車の車軸穴',a,holes,(0,1,0),24.1 if m.detailed else 28,g,'黒')
        slots=[m.rect('輪軸下開き溝',(cx+dx-.96,-12.05 if m.detailed else -14,3.8),(1,0,0),(0,0,1),1.92,1.575,g) for dx in [-13.125,13.125]]
        a=m.cut(lab+'台車の下開き',a,slots,(0,1,0),24.1 if m.detailed else 28,g,'黒')
        hole=m.circle('台車心皿穴',(cx,0,6.1),(0,0,1),1.1,g)
        a=m.cut(lab+'心皿穴を貫通',a,[hole],(0,0,1),8,g,'黒')
        m.keyparts['bogies'].append(a);m.clear(a,m.finished['floor'],lab+'台車と床板')
        if m.detailed:
            for dx in [-13.125,13.125]:
                for side in [-1,1]:
                    for z in [4.8,5.65]:m.points.append(dict(entity=a,label='黒い軸箱外蓋を残す',
                        point=[cx+dx,side*12.3,z],expectedInside=True))
        for dx in [-13.125,13.125]:
            ax=m.cylinder('車軸',(cx+dx,-11.85 if m.detailed else -12.6,5.375),(0,1,0),.8,23.7 if m.detailed else 25.2,g);parts=[ax]
            for side in [-1,1]:
                parts += [m.cylinder('車輪踏面',(cx+dx,-10.0 if side<0 else 8.2,5.375),(0,1,0),5.375,1.8,g),
                          m.cylinder('フランジ',(cx+dx,-8.2 if side<0 else 7.25,5.375),(0,1,0),5.65,.95,g),
                          m.cylinder('車輪内側ディスク',(cx+dx,side*4.2-.22,5.375),(0,1,0),3.4,.44,g)]
            ax=m.joined(lab+'輪軸・内側二枚ディスク',parts,g,'金属銀',note='HO16.5用機構。ディスク径は未確定の外観設計値。絶縁・走行未試験。')
            m.keyparts['wheelsets'].append(ax);m.clear(a,ax,lab+'台車と輪軸')
        for side in [-1,1]:
            if m.detailed:
                for dx in [-13.125,13.125]:
                    keeper=m.box(lab+'局所輪軸押え素材',cx+dx-1.8,side*11.5-1.2,3.7,3.6,2.4,.8,g)
                    keeper=m.boolean(lab+'局所輪軸押え',keeper,[a],1,g,'黒')
                    m.entities[a]['visibility']='visible';m.clear(a,keeper,lab+'台車と着脱輪軸押え')
            else:m.finish(m.box(lab+'輪軸押え',cx-15.3,side*12.0-1.2,3.7,30.6,2.4,.55,g),'黒',.55)
        if lab=='前':
            p=m.poly('スノープロウ板',[(cx-20.5,-13.6,3.8),(cx-20.5,13.6,3.8),(cx-19.5,13.6,6.6),(cx-19.5,-13.6,6.6)],g)
            plow=m.extrude('前台車スノープロウ',[p],(1,0,0),.45,g)
            stays=[m.box('プロウ支持腕',cx-20.1,yy,6.2,10.1,1.0,.6,g) for yy in [-6.5,5.5]]
            plow=m.joined('前台車プロウ・支持腕',[plow]+stays,g,'黒')
            for wheel in m.keyparts['wheelsets'][:2]:m.clear(plow,wheel,'前プロウと輪軸')


def interior(m,root):
    g=m.group('11 室内｜対向座席・仕切・運転台',root)
    p=m.rect('室内床',(4,-15.5,15.15),(1,0,0),(0,1,0),L-8,31,g)
    holes=[m.circle('室内心皿逃げ',(x,0,15.15),(0,0,1),1.4,g) for x in PIVOTS]
    tray=m.extrude('室内床',[p]+holes,(0,0,1),.4,g);parts=[tray]
    for cx in [80.5,99.125,155.5,174.125]:
        for y in [-15.3,5.2]:
            for direction in [-1,1]:
                bx=cx+direction*6.0;z=19.4
                parts += [m.box('対向座席座面',bx-2.8,y,z,5.6,10.1,.9,g),
                          m.box('座席背面',bx+(2.2 if direction>0 else -2.9),y,z+.8,.7,10.1,5.3,g),
                          m.box('座席脚',bx-.6,y+1.2,15.5,1.2,7.7,3.9,g)]
    for x in [60,111,135,186,212]:
        for y in [-15.3,12]:parts += [m.box('ドア脇ロング席',x,y,19.2,7.8,3.3,1.1,g),
                                     m.box('ロング席背面',x,-15.3 if y<0 else 14.7,20.2,7.8,.6,4.6,g),
                                     m.box('ロング席支持',x+1.0,y+.5,15.5,5.8,2.3,3.7,g)]
    o=m.rect('運転室仕切外',(25.3,-15.5,15.55),(0,1,0),(0,0,1),31,22.0,g)
    q=m.rounded('仕切通路開口',(25.3,-3.0,15.3),(0,1,0),(0,0,1),6.0,19.25,.4,g)
    bulkhead=m.extrude('運転室仕切素材',[o],(1,0,0),.6,g)
    parts.append(m.cut('運転室仕切・通路',bulkhead,[q],(1,0,0),.6,g))
    parts += [m.box('運転台机',4,-14.9,23.0,4.5,10.0,1.4,g),m.box('運転台支持',6,-14.5,15.5,1.3,9,7.5,g)]
    # A complete toilet cubicle with a real aisle, rather than one arbitrary panel.
    parts += [m.box('便所前仕切',230.0,5.0,15.55,.65,10.5,20.4,g),
              m.box('便所通路側壁',230.0,5.0,15.55,10.5,.65,20.4,g)]
    a=m.joined('室内トレー・座席・運転台・便所',parts,g,'室内クリーム')
    m.finished['interior']=a;m.clear(a,m.finished['floor'],'室内と床板');m.clear(a,m.finished['body'],'室内と車体')
    for glass in m.keyparts['glasses']:m.clear(a,glass,'室内と透明板')


def front_underframe(m):
    """Recessed end web, connected to the printable floor; centre clears the coupler."""
    floor=m.finished['floor'];g=m.entities[floor]['groupId']
    webs=[m.box('前端台枠の奥まった縦板',3.4,y,11.0,2.2,10.6,2.70,g)
          for y in [-12.7,2.1]]
    floor=m.joined('床板・前端台枠一体',[floor]+webs,g,'床下グレー',
        note='前端台枠は写真の奥まった支持構造を模型用に簡略化。実測寸法ではない。中央は連結器逃げ。')
    m.finished['floor']=floor
    m.clear(floor,m.finished['body'],'前端台枠付き床板と外板')
    m.clear(floor,m.finished['interior'],'前端台枠付き床板と室内')
    for y in [-6,6]:
        m.points.append(dict(entity=floor,label='前端台枠の奥まった支持板',point=[4,y,12],expectedInside=True))
    m.points.extend([dict(entity=floor,label='前端台枠の中央連結器逃げ',point=[4,0,12],expectedInside=False),
                     dict(entity=floor,label='前端台枠の下側空間',point=[4,6,10.7],expectedInside=False)])


def manifest(m):
    data=dict(scale=80,gaugeMm=16.5,prototype=dict(family='115-1000',carCandidate='クハ115-1222',
        period='長野色営業時代（1994〜97年頃を外装候補）',status='写真資料を基にした外観設計案。車番固有の全寸法は未確定。'),
        dimensions=dict(bodyLength=L,maximumWidth=36.875,roofHeight=CROWN,floorHeight=FLOOR,coupledLength=250,bogieCentres=172.5),
        estimated=dict(frontFoldDeg=8,frontCornerRadiusMm=3.8,skirtTaperMm=1.1,doorCentres=DOORS,
                       gangwayWindow=GW,sideWindows={str(k):v for k,v in m.layout.items()},
                       note='局所寸法は実測値ではない。原写真の形状を照合した模型設計値。'),
        evidence=dict(audit='docs/development/kuha115-prototype-audit-20261024.html',
                      userCorrection='2026-10-24: 正面貫通扉窓は横長寄り。幅7.2/高さ5.8=1.241。扉全高とは独立。'),
        stlChordMm=.025,stlChordByEntity=m.mesh_chords,colors=m.colors,groups=m.d['groups'],
        assemblyChecks=m.checks,pointChecks=m.points,keyParts=m.keyparts,
        entities=[{k:e[k] for k in ['id','kind','displayName','visibility','groupId','partProperties']} for e in m.d['entities']])
    if m.detailed:
        data['detailRevision']='2026-10-25-shoulder-refinement'
        data['paintDecals']=m.decals
        data['estimated'].update(panoramicWindow=dict(bottom=30.6,height=7.6),destination=dict(width=9.3,height=3.95),
            climateCrossSection='YZ arch extruded along X',wheelsetAxleLength=23.7,localAxleKeepers=True,
            foreheadLongitudinalRadius=9.0,foreheadCurveOriginZ=CROWN-9.0,roofNominalThickness=1.0,
            foreheadConstruction='short identical analytic transverse arcs translated on a concentric longitudinal arc')
        data['estimated'].update(skirtOutline=dict(topHalfWidth=17.1,bottomHalfWidth=16.0,bottomZ=6.6,centreTopZ=10.7),
                                 flareCapRadius=.78,frontGlazingInset=.22,greenPaintBandZMm=[22.4,24.0],
                                 frontUnderframeWeb=dict(x=[3.4,5.6],z=[11.0,13.70],centralClearance=4.2))
    return data


def split_model(original,root,output):
    m=copy.deepcopy(original);m.d['documentId']=str(uuid.uuid5(m.namespace,'revised-split'))
    g=m.group('12 小型プリンタ用｜前後分割・床板添え板',root)
    replacements={};checks=[];mid=L/2
    for key in ['body','roof','floor','interior','gutter-1','gutter1']:
        source=m.finished[key];name=m.entities[source]['displayName'];color=m.colors[source]
        p=m.rect('前半用の切断工具',(mid,-25,-2),(0,1,0),(0,0,1),50,60,g)
        first=m.cut(name+' 前半',source,[p],(1,0,0),L-mid+5,g,color)
        p=m.rect('後半用の切断工具',(-5,-25,-2),(0,1,0),(0,0,1),50,60,g)
        second=m.cut(name+' 後半',source,[p],(1,0,0),mid+5,g,color)
        replacements[source]=[first,second]
        checks.append(dict(source=source,front=first,rear=second,label=key))
        m.clear(first,second,key+' 分割片同士に重なりなし')
    expanded=[]
    for c in m.checks:
        for first in replacements.get(c['first'],[c['first']]):
            for second in replacements.get(c['second'],[c['second']]):expanded.append(dict(c,first=first,second=second))
    m.checks=expanded
    for c in m.points:
        if c['entity'] in replacements:c['entity']=replacements[c['entity']][int(c['point'][0]>mid)]
    bridge=m.finish(m.box('床板継ぎ目の添え板',mid-6,-4,12.9,12,8,.75,g),'床下グレー',.75)
    for p in replacements[original.finished['floor']]:m.clear(bridge,p,'添え板と床板')
    data=manifest(m);data.update(splitChecks=checks,splitAtX=mid)
    m.save(output/'kuha115-ho80-revised-split.kcd2')
    (output/'design-split.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')


def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',required=True,type=Path)
    ap.add_argument('--detailed',action='store_true',help='Use photo-reviewed front, roof equipment and bogie details')
    a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    m=Revised(a.detailed);root=m.group('クハ115 長野色｜調査反映の新規設計 1/80 HO16.5')
    body(m,root);side_details(m,root);front(m,root);roof(m,root);chassis(m,root);bogies(m,root);interior(m,root)
    if m.detailed:
        front_underframe(m)
        from detail_parts import lettering
        lettering(m,root,a.output)
    m.d['activeGroupId']=root;m.d['uiState']={'activeMode':'part','theme':'windows95'}
    m.d['grid']=dict(visible=False,workPlaneId=None,originUvMm=dict(x=0,y=0),majorSpacingMm=10.0,subdivision='none')
    m.save(a.output/'kuha115-ho80-revised.kcd2')
    (a.output/'design.json').write_text(json.dumps(manifest(m),ensure_ascii=False,indent=2),encoding='utf-8',newline='\n')
    split_model(m,root,a.output)
    print(json.dumps(dict(features=len(m.d['features']),parts=sum(e['kind']=='part' and e['visibility']=='visible' for e in m.d['entities']),
                         checks=len(m.checks),pointChecks=len(m.points),gangwayRatio=GW['width']/GW['height'])))


if __name__=='__main__':main()
