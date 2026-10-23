"""Native-feature Kuha 115-1000 print design. All lengths are model millimetres."""
from pathlib import Path
import sys, math, json, base64, argparse, copy
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/modeling'))
from native_kcd import Model,expr,vec

L=243.75; Y=18.4375; FLOOR=15.3125; EAVE=39.6; ROOF=45.675
DOORS=[47.5,123.75,200.0]
WINDOWS=[(29.5,10.5),(64.2,9.0375),(80.0,13.525),(97.0,13.525),(110.0,9.0375),
         (140.1,9.0375),(155.9,13.525),(172.9,13.525),(186.0,9.0375),(216.2,9.0375),(232.0,13.525)]

class Train(Model):
    def __init__(self):
        super().__init__('クハ115-1000 長野色 リニューアル車 1/80 印刷・組立用')
        self.d['metadata'].update(author='kachakachaCAD',description='Native editable model. Official major dimensions; estimated local detail dimensions are listed in design.json. Black H rubber, no pantograph, unpowered control trailer.')
        self.d['documentId']=self.id();self.colors={};self.print_groups={};self.fitPairs=[]
    def finish(self,eid,color='フォギーグレー',wall=None,note=''):
        p={'materialName':'光造形用レジン（設計基準）','colorName':color,'processName':'3Dプリント・塗装・接着組立','notes':note,'layerCount':1,'referenceScaleDenominator':80}
        if wall:p['nominalThicknessMm']=wall
        self.entities[eid]['partProperties']['manufacturing']=p
        self.colors[eid]=color
        return eid
    def boolean(self,name,target,tools,mode,g,color='フォギーグレー'):
        result=self.add(name,'part','boolean',{'mode':mode,'targets':[target],'tools':tools},g,[target]+tools)
        for old in [target]+tools:self.entities[old]['visibility']='hidden'
        return self.finish(result,color)
    def cut(self,name,target,profiles,n,length,g):
        result=self.add(name,'part','extrude',{'profiles':profiles,'direction':vec(n),'distance':expr(length),'extentMode':0,'booleanMode':2,'targets':[target]},g,profiles+[target])
        self.entities[target]['visibility']='hidden'
        return self.finish(result)
    def perimeter(self,z,inset=0,front=0):
        # Rounded front quarters, square rear corners. The centre front is tangent to both quarters.
        a=front+inset;b=L-inset;y=Y-inset;r=3.6-inset
        return [self.line((a+r,-y,z),(b,-y,z)),self.line((b,-y,z),(b,y,z)),
            self.line((b,y,z),(a+r,y,z)),self.arc((a+r,y-r,z),(0,0,1),(1,0,0),r,math.pi/2,math.pi/2),
            self.line((a,y-r,z),(a,-y+r,z)),self.arc((a+r,-y+r,z),(0,0,1),(1,0,0),r,math.pi,math.pi/2)]
    def shell(self,name,g,thickness=.85):
        outer=self.wire(name+' 外周',self.perimeter(14.3),g)
        inner=self.wire(name+' 内周',self.perimeter(14.3,thickness),g)
        return self.finish(self.extrude(name,[outer,inner],(0,0,1),EAVE-14.3,g),wall=thickness)
    def image(self,name,path,o,u,v,g):
        from PIL import Image
        im=Image.open(path)
        return self.add(name,'image','create_image',{'pngBase64':base64.b64encode(path.read_bytes()).decode(),
            'pixelWidth':im.width,'pixelHeight':im.height,'mmPerPixel':.1,'rotationRad':0,'opacity':.5,
            'mirrorHorizontal':False,'followSurface':False,'faceBrep':'','origin':vec(o),'uAxis':vec(u),'vAxis':vec(v),
            'anchorPixel':vec((0,im.height,0)),'anchorUv':vec((0,0,0)),'uvMetric':vec((1,1,0))},g,visible=False)

def body(M,root):
    g=M.group('01 車体・前面｜一体外板 0.85mm',root)
    shell=M.shell('車体外板（前面丸みR3.6）',g)
    for sign,label in [(1,'右'),(-1,'左')]:
        for i,(x,w) in enumerate(WINDOWS):
            if sign==-1 and i==10:continue # toilet end: separate small frosted opening below
            p=M.rounded(label+f'客室窓{i+1:02} 開口',(x-w/2,sign*(Y+1),27),(1,0,0),(0,0,1),w,10.6,.65,g)
            shell=M.cut(label+f'窓{i+1:02} を抜く',shell,[p],(0,-sign,0),3,g)
        openings=[]
        for i,x in enumerate(DOORS):openings.append(M.rounded(label+f'客扉{i+1} 開口',(x-8.125,sign*(Y+1),FLOOR),(1,0,0),(0,0,1),16.25,23,.45,g))
        openings.extend([M.rounded(label+'乗務員扉開口',(14,sign*(Y+1),FLOOR),(1,0,0),(0,0,1),7.8,22.5,.5,g),
            M.rounded(label+'運転室側窓開口',(5.1,sign*(Y+1),29.4),(1,0,0),(0,0,1),7.1,8.0,.65,g)])
        if sign==-1:openings.append(M.rounded('便所窓 開口',(228,sign*(Y+1),30.2),(1,0,0),(0,0,1),7,6.3,.6,g))
        shell=M.cut(label+'扉・運転室開口',shell,openings,(0,-sign,0),3,g)
    holes=[]
    for sign in [-1,1]:
        y=-16.35 if sign<0 else 5.25
        holes.append(M.rounded('前面窓 開口',(-2,y,29.1),(0,1,0),(0,0,1),11.1,9.8,1.05,g))
    holes.append(M.rounded('貫通扉 開口',(-2,-4.1,FLOOR),(0,1,0),(0,0,1),8.2,23.1,.8,g))
    shell=M.cut('前面窓・貫通口を抜く',shell,holes,(1,0,0),8,g)
    p=M.rounded('後妻貫通口',(L+1,-4.1,FLOOR),(0,1,0),(0,0,1),8.2,23.1,.8,g)
    shell=M.cut('妻面貫通口を抜く',shell,[p],(-1,0,0),3,g)
    M.finish(shell,wall=.85,note='底・屋根は開放。前面窓の外端は丸い前面隅へ回り込む。内側に0.85mm。')
    tabs=[M.box('屋根位置決め内側桟',6,y,38.4,L-12,1,1.2,g) for y in [-17.85,16.85]]
    for x,w,t,name in [(0,Y-3.6,43.8,'前面上部'),(L-.85,Y,ROOF,'後妻上部')]:
        c=(w*w+EAVE*EAVE-t*t)/(2*(EAVE-t));r=t-c;a=math.asin(w/r)
        arc=M.arc((x,0,c),(1,0,0),(0,0,1),r-.9,-a,2*a)
        p0=(x,(r-.9)*math.sin(a),c+(r-.9)*math.cos(a));p1=(x,-p0[1],p0[2])
        p=M.wire(name+' 屋根内側に合う輪郭',[arc,M.line(p1,p0)],g)
        tabs.append(M.extrude(name,[p],(1,0,0),.85,g))
    shell=M.boolean('車体 一体外板＋屋根位置決め桟',shell,tabs,0,g)
    return M.finish(shell,wall=.85,note='車体内部に連結した位置決め桟。床板左右すき間0.25mm。')

def side_details(M,root):
    sash=M.group('03 窓サッシ｜銀・左右別',root);glass=M.group('04 窓ガラス｜透明・左右別',root)
    doors=M.group('05 扉・側面小物',root);gutters=[]
    for sign,label in [(1,'右'),(-1,'左')]:
        sg=M.group(label+' 客室サッシ',sash);gg=M.group(label+' 客室ガラス',glass);dg=M.group(label+' 扉・雨樋',doors)
        for i,(x,w) in enumerate(WINDOWS):
            z,h=27,10.6
            if sign==-1 and i==10:x,w,z,h=231.5,7,30.2,6.3
            y=sign*(Y+.35)
            o=M.rounded(label+f'サッシ{i+1:02} 外周',(x-w/2-.17,y,z-.17),(1,0,0),(0,0,1),w+.34,h+.34,.78,sg)
            q=M.rounded(label+f'サッシ{i+1:02} 内周',(x-w/2+.22,y,z+.22),(1,0,0),(0,0,1),w-.44,h-.44,.5,sg)
            frame=M.finish(M.extrude(label+f'サッシ{i+1:02}',[o,q],(0,-sign,0),.35,sg),'アルミ銀',.35)
            p=M.rounded(label+f'ガラス{i+1:02}',(x-w/2-.30,sign*(Y-.95),z-.30),(1,0,0),(0,0,1),w+.60,h+.60,.78,gg)
            pane=M.finish(M.extrude(label+f'透明板{i+1:02}',[p],(0,-sign,0),.5,gg),'透明',.5,note='透明レジンか透明板で製作。')
            if h>8:
                bar=M.box(label+f'窓中桟{i+1:02}',x-w/2+.15,sign*Y-(.35 if sign<0 else 0),32.05,w-.3,.35,.35,sg)
                frame=M.boolean(label+f'サッシ{i+1:02}（中桟一体）',frame,[bar],0,sg,'アルミ銀')
            M.fitPairs.append({'first':frame,'second':pane,'label':label+f'サッシ・透明板{i+1:02}','noOverlap':True})
        for i,x in enumerate(DOORS):
            for half in [-1,1]:
                x0=x-8.0 if half<0 else x+.05;y=sign*(Y-.3)
                o=M.rounded('扉葉',(x0,y,FLOOR+.1),(1,0,0),(0,0,1),7.95,22.8,.4,dg)
                h=M.rounded('扉窓',(x0+1.45,y,28.5),(1,0,0),(0,0,1),5.1,8.6,.7,dg)
                M.finish(M.extrude(label+f'客扉{i+1} {half}',[o,h],(0,-sign,0),.7,dg),'フォギーグレー',.7)
                p=M.rounded('扉ガラス',(x0+1.30,sign*(Y-1.10),28.35),(1,0,0),(0,0,1),5.4,8.9,.8,gg)
                M.finish(M.extrude(label+'扉透明板',[p],(0,-sign,0),.5,gg),'透明',.5)
        o=M.rounded('乗務員扉',(14.1,sign*(Y-.35),FLOOR+.1),(1,0,0),(0,0,1),7.6,22.3,.5,dg)
        h=M.rounded('乗務員扉窓',(15.25,sign*(Y-.35),29.7),(1,0,0),(0,0,1),5.3,7,.6,dg)
        M.finish(M.extrude(label+'乗務員扉',[o,h],(0,-sign,0),.7,dg),'フォギーグレー',.7)
        for x,w in [(4.8,7.7),(14.95,5.9)]:
            p=M.rounded('運転室ガラス',(x,sign*(Y-1.10),29.1 if x<10 else 29.4),(1,0,0),(0,0,1),w,8.6 if x<10 else 7.6,.8,gg)
            M.finish(M.extrude(label+'運転室透明板',[p],(0,-sign,0),.5,gg),'透明',.5)
        gutters.append(M.finish(M.box(label+'雨樋',6,sign*Y-.25,38.5,L-6,.5,.45,dg),'フォギーグレー',.45))
        for x in [13.0,22.4]:M.finish(M.tube(label+'手すり',(x,sign*(Y+.3),22),(x,sign*(Y+.3),29),.2,dg),'アドバンスブルー',.4)
    return gutters

def front_details(M,root):
    fg=M.group('06 前面｜Hゴム・ガラス・貫通扉',root)
    # A narrow offset shell is intersected with a planar window beam to retain curved corner geometry.
    for side in [-1,1]:
        y=-16.35 if side<0 else 5.25
        for label,outerInset,innerInset,color in [('Hゴム',-.35,0,'黒'),('透明窓',.95,1.45,'透明')]:
            o=M.wire(label+' 曲面外周',M.perimeter(28.7,outerInset),fg)
            i=M.wire(label+' 曲面内周',M.perimeter(28.7,innerInset),fg)
            base=M.extrude(label+' 曲面素材',[o,i],(0,0,1),10.6,fg)
            if label=='Hゴム':
                p=M.rounded('ゴム外周',(-2,y-.28,28.82),(0,1,0),(0,0,1),11.66,10.36,1.25,fg)
                q=M.rounded('ゴム内周',(-2,y+.15,29.25),(0,1,0),(0,0,1),10.8,9.5,.95,fg)
                beam=M.extrude('ゴム輪郭切り出し',[p,q],(1,0,0),9,fg)
            else:
                p=M.rounded('ガラス切り出し',(-2,y-.30,28.8),(0,1,0),(0,0,1),11.7,10.4,1.2,fg)
                beam=M.extrude('ガラス輪郭切り出し',[p],(1,0,0),9,fg)
            M.boolean(('左' if side<0 else '右')+'前面'+label,base,[beam],2,fg,color)
        M.finish(M.tube('ワイパー',(-.45,y+4,29.6),(-.45,y+2.2,34.4),.21,fg),'黒',.42)
        M.finish(M.tube('ワイパーブレード',(-.55,y+.9,34),(-.55,y+4.5,35.1),.22,fg),'黒',.44)
    o=M.rounded('貫通扉外形',(-.12,-3.95,FLOOR+.1),(0,1,0),(0,0,1),7.9,22.9,.7,fg)
    h=M.rounded('貫通扉窓',(-.12,-2.7,29.0),(0,1,0),(0,0,1),5.4,8.4,.65,fg)
    M.finish(M.extrude('貫通扉',[o,h],(1,0,0),.85,fg),'フォギーグレー',.85)
    p=M.rounded('貫通扉ガラス',(.83,-2.9,28.8),(0,1,0),(0,0,1),5.8,8.8,.8,fg)
    M.finish(M.extrude('貫通扉透明板',[p],(1,0,0),.5,fg),'透明',.5)
    M.finish(M.box('貫通扉取手',-.45,2.7,24.8,.45,.45,1.4,fg),'アルミ銀',.45)
    M.finish(M.box('渡り板',-1.5,-4.3,FLOOR-.4,1.8,8.6,.65,fg),'アドバンスブルー',.65)
    for side in [-1,1]:
        g=M.group(('左' if side<0 else '右')+' 灯具',fg)
        for name,z,r in [('前照灯',25.5,1.82),('尾灯',20.3,.95)]:
            M.finish(M.ring(name+'リム',(-.75,side*12.1,z),(1,0,0),r,r-.33,.9,g),'アルミ銀',.33)
            M.finish(M.cylinder(name+'レンズ',(-.78,side*12.1,z),(1,0,0),r-.38,.5,g),'赤透明' if name=='尾灯' else '透明',.5)
    M.finish(M.box('前面表示器ケース',-.2,-4.6,40.05,.65,9.2,2.3,fg),'フォギーグレー',.65)

def roof(M,root):
    g=M.group('02 屋根｜前頭ドーム・取外し式',root);refs=M.group('屋根断面・参照面',g)
    def section(x,w,t):
        c=(w*w+EAVE*EAVE-t*t)/(2*(EAVE-t));r=t-c;a=math.asin(w/r)
        return M.wire('屋根断面 '+str(x),[M.arc((x,0,c),(1,0,0),(0,0,1),r,-a,2*a)],refs)
    stations=[]
    for x in [0,.2,.5,1,1.6,2.4,3.0,3.6]:
        f=math.sqrt(max(0,1-(1-x/3.6)**2))
        stations.append((x,Y-3.6+3.6*f,43.8+(ROOF-43.8)*f))
    stations.append((3.9,Y,ROOF))
    sections=[section(x,w,z) for x,w,z in stations]
    guide=M.guide('前頭から連続する曲面',sections,2,[2]*len(sections),refs)
    bands=[]
    for x,w,t in stations:
        c=(w*w+EAVE*EAVE-t*t)/(2*(EAVE-t));r=t-c;a=math.asin(w/r)
        def pt(radius,angle):return (x,-radius*math.sin(angle),c+radius*math.cos(angle))
        bands.append(M.wire('屋根閉断面 '+str(x),[M.arc((x,0,c),(1,0,0),(0,0,1),r,-a,2*a),
            M.line(pt(r,a),pt(r-.9,a)),M.arc((x,0,c),(1,0,0),(0,0,1),r-.9,a,-2*a),M.line(pt(r-.9,-a),pt(r,-a))],refs))
    r=M.add('前頭ドーム 閉断面ロフト','part','create_solid',{'method':1,'profiles':bands,'axis':None,'path':[],
        'angleRad':2*math.pi,'symmetric':False,'booleanMode':0,'targets':[]},g,bands)
    main=M.extrude('屋根一般部 円弧一定断面',[bands[-2]],(1,0,0),L-3.6,g)
    r=M.boolean('屋根 一体ドーム＋円弧一般部',r,[main],0,g,'屋根グレー')
    M.finish(r,'屋根グレー',.9,'各断面の円弧半径差0.9mm。前頭部と一般部を連続した閉断面ロフトで構成。車体とは別出力。')
    equipment=M.group('07 屋根上｜AU75・通風器',root)
    cooler=M.finish(M.box('AU75 冷房装置',100,-10.6,45.65,43.5,21.2,3.1,equipment),'屋根グレー',1)
    grids=[]
    for x in [108,121.7,135.4]:
        grids.append(M.ring('冷房送風機ガード',(x,0,48.70),(0,0,1),3.3,2.9,.45,equipment))
        for y in [-1.5,0,1.5]:grids.append(M.box('送風機格子',x-2.6,y-.2,48.70,5.2,.4,.4,equipment))
    M.boolean('AU75 格子・ガード一体',cooler,grids,0,equipment,'屋根グレー')
    for x in [28,61,81,168,195,224]:
        p=M.poly('押込通風器断面',[(x-3.6,-3,45.5),(x+3.6,-3,45.5),(x+2.9,-3,47.2),(x-2.9,-3,47.2)],equipment)
        M.finish(M.extrude('押込通風器',[p],(0,1,0),6,equipment),'屋根グレー',.8)
    M.finish(M.cylinder('無線アンテナ基部',(12,0,45.7),(0,0,1),.7,.8,equipment),'屋根グレー',.8)
    M.finish(M.cylinder('無線アンテナ',(12,0,46.5),(0,0,1),.3,2.7,equipment),'屋根グレー',.6)
    for y in [-6,6]:M.finish(M.cylinder('信号炎管・ホイッスル',(6,y,44.9),(0,0,1),.5,1.7,equipment),'屋根グレー',1)
    return r

def chassis(M,root):
    floor=M.group('08 床板｜台車中心・ねじ穴',root)
    p=M.wire('床板輪郭（前隅R・全周0.25mm逃げ）',M.perimeter(13.9,1.1),floor)
    pivots=[33.4375,210.3125]
    holes=[M.circle('台車 M2.2穴',(x,0,13.9),(0,0,1),1.1,floor) for x in pivots]
    M.finish(M.extrude('床板 1.2mm',[p]+holes,(0,0,1),1.2,floor),'床下グレー',1.2)
    under=M.group('09 床下｜クハ機器・スカート・連結器',root)
    for x,y,w,d,h in [(67,-15,22,10,5),(99,-15,28,10,4.8),(146,-15,25,10,5),
                       (70,4,25,10,4.8),(106,5,18,10,5),(157,5,21,9,4.7)]:
        box=M.box('クハ床下機器箱（配置推定）',x,y,8.3,w,d,h,under)
        feet=[M.box('機器吊り脚',xx,y+1,8.3+h-.1,1.2,d-2,13.9-(8.3+h)+.1,under) for xx in [x+1,x+w-2.2]]
        M.boolean('床下機器 吊り脚一体',box,feet,0,under,'床下グレー')
    for x,y in [(54,-4),(180,-6),(183,6)]:
        tank=M.cylinder('空気溜',(x,y,10.3),(1,0,0),2.1,12,under)
        feet=[M.box('空気溜吊り脚',xx,y-.7,11.8,1.2,1.4,2.1,under) for xx in [x+1,x+9.8]]
        M.boolean('空気溜 吊り脚一体',tank,feet,0,under,'床下グレー')
    for sign in [-1,1]:
        p=M.poly('スカート', [(-.8,sign*2.7,8.1),(-.8,sign*15.4,8.1),(-.8,sign*16.4,13.7),(-.8,sign*2.7,13.7)],under)
        skirt=M.extrude('スカート 左右',[p],(1,0,0),.9,under)
        tab=M.box('スカート取付座',-.2,sign*8-1,12.7,3.2,2,1.2,under)
        M.boolean('スカート 接着座一体',skirt,[tab],0,under,'床下グレー')
    for front,x in [(True,-3.125),(False,L+2.325)]:
        head=M.box('連結器頭',x,-1.5,9.5,.8,3,2.2,under)
        start=-2.4 if front else L-4
        stem=M.box('連結器柄',start,-.65,10.0,6.8,1.3,1.3,under)
        mount=M.box('連結器取付座',2 if front else L-3.5,-1.5,10.9,2,3,3,under)
        M.boolean('連結器 接着座一体',head,[stem,mount],0,under,'床下グレー')
    bg=M.group('10 台車｜TR62A・展示用固定台車',root)
    for x,label in zip(pivots,['前台車','後台車']):
        g=M.group(label,bg);pieces=[]
        core=M.box('枕梁',x-2,-12,9,4,24,3,g)
        pieces.append(M.cylinder('心皿取付座',(x,0,11.8),(0,0,1),2.6,2.1,g))
        for sign in [-1,1]:
            y=sign*12.5
            pts=[(x-15,y,6.5),(x-15,y,9.4),(x-10,y,10.2),(x-5,y,9.4),(x+5,y,9.4),(x+10,y,10.2),(x+15,y,9.4),(x+15,y,6.5),(x+7,y,6.5),(x+4,y,7.7),(x-4,y,7.7),(x-7,y,6.5)]
            p=M.poly('TR62A側枠 外形推定',pts,g)
            pieces.append(M.extrude('TR62A 側枠',[p],(0,-sign,0),1.5,g))
            for dx in [-13.125,13.125]:
                pieces.append(M.box('軸箱',x+dx-1.5,y-1.1,4.2,3,2.2,3.1,g))
                for sx in [-2.4,2.4]:pieces.append(M.cylinder('軸ばね一体表現',(x+dx+sx,y-sign*.3,7.1),(0,0,1),.75,2.2,g))
        for dx in [-13.125,13.125]:
            pieces.append(M.cylinder('車軸',(x+dx,-12,5.375),(0,1,0),.8,24,g))
            for sign in [-1,1]:pieces.append(M.cylinder('車輪',(x+dx,-10.15 if sign<0 else 8.35,5.375),(0,1,0),5.375,1.8,g))
        bogie=M.boolean(label+' TR62A 展示用一体台車',core,pieces,0,g,'床下グレー')
        p=M.circle('台車 M2.2穴',(x,0,8.8),(0,0,1),1.1,g)
        bogie=M.cut(label+' ねじ穴付き',bogie,[p],(0,0,1),6,g)
        M.finish(bogie,'床下グレー',note='輪軸・枠が一体の展示用。転がり・集電・規格車輪ではない。床板までの心皿付き。')
    inside=M.group('11 室内｜脚・取付トレー一体',root)
    p=M.rect('室内トレー',(2.3,-16.9,15.1),(1,0,0),(0,1,0),L-4.6,33.8,inside)
    holes=[M.circle('台車ねじ逃げ',(x,0,15.1),(0,0,1),1.3,inside) for x in pivots]
    tray=M.extrude('室内トレー',[p]+holes,(0,0,1),.4,inside);furniture=[]
    furniture.append(M.box('運転室仕切り',23,-16.8,FLOOR,.8,33.6,23,inside))
    furniture.append(M.box('運転台',3,-15,FLOOR+7,6,13,3.2,inside))
    for y in [-14,-4]:furniture.append(M.box('運転台脚',3.5,y,15.4,1,1,7,inside))
    for x in [78,96,154,172]:
        for sign in [-1,1]:
            y=sign*11.4-4
            furniture.append(M.box('座席座面',x-3,y,FLOOR+3.8,6,8,1,inside))
            furniture.append(M.box('座席背',x+2.4,y,FLOOR+4.8,.9,8,6,inside))
            for yy in [y+1,y+6]:furniture.append(M.box('座席脚',x-1,yy,15.4,1,1,4.2,inside))
    furniture.append(M.box('後位便所仕切り',224,-16.8,FLOOR,.9,12,21,inside))
    M.boolean('室内 トレー・座席・運転台一体',tray,furniture,0,inside,'室内クリーム')

def split_model(source,out,root,design):
    M=copy.deepcopy(source);M.d['documentId']=M.id();M.d['metadata']['title']+='｜小型プリンタ分割版'
    g=M.group('13 小型プリンタ用｜4部位の前後分割',root)
    cut=123.75
    wanted=[e for e in M.d['entities'] if e['kind']=='part' and e['visibility']=='visible'
        and e['displayName'] in ['車体 屋根取付座・雨樋一体','屋根 一体ドーム＋円弧一般部','床板 1.2mm','室内 トレー・座席・運転台一体']]
    assert len(wanted)==4
    splitChecks=[]
    for e in wanted:
        name=e['displayName'];target=e['id'];color=M.colors.get(target,'フォギーグレー');gg=M.group(name+' 分割',g)
        p=M.rect('前半切断',(cut,-30,-2),(0,1,0),(0,0,1),60,65,gg)
        front=M.cut(name+' 前半',target,[p],(1,0,0),L-cut+10,gg);M.finish(front,color)
        p=M.rect('後半切断',(-5,-30,-2),(0,1,0),(0,0,1),60,65,gg)
        rear=M.cut(name+' 後半',target,[p],(1,0,0),cut+5,gg);M.finish(rear,color)
        splitChecks.append({'source':target,'front':front,'rear':rear,'label':name})
    M.finish(M.box('床板継ぎ目 内側添え板',cut-6,-4,12.9,12,8,1,g),'床下グレー',1,'床板下面に接着。床板を組立基準にして車体の前後を合わせる。')
    M.save(out/'kuha115-1000-nagano-1-80-split.kcd2')
    design=copy.deepcopy(design);design.update(colors=M.colors,groups=M.d['groups'],splitAtX=cut,splitChecks=splitChecks,
        entities=[{k:e[k] for k in ['id','kind','displayName','visibility','groupId','partProperties']} for e in M.d['entities']])
    (out/'design-split.json').write_text(json.dumps(design,ensure_ascii=False,indent=2),encoding='utf-8')

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,default=Path(__file__).parent/'model');args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=True)
    M=Train();root=M.group('クハ115-1000 長野色 1/80')
    mainBody=body(M,root);mainRoof=roof(M,root)
    g=M.entities[mainBody]['groupId']
    straightRoof=next(e['id'] for e in M.d['entities'] if e['displayName']=='屋根一般部 円弧一定断面' and e['kind']=='part')
    fitted=M.boolean('車体 屋根内面に合う取付座',mainBody,[straightRoof],1,g)
    M.finish(fitted,wall=.85,note='屋根一般部の形状で上端を切り欠く。前頭部は接着組立用の合わせ代。')
    gutters=side_details(M,root)
    fitted=M.boolean('車体 屋根取付座・雨樋一体',fitted,gutters,0,g)
    front_details(M,root);chassis(M,root)
    floor=next(e['id'] for e in M.d['entities'] if e['displayName']=='床板 1.2mm' and e['kind']=='part')
    M.fitPairs.append({'first':fitted,'second':floor,'label':'車体・床板（前隅と側面）','noOverlap':True})
    M.fitPairs.append({'first':fitted,'second':mainRoof,'label':'車体・屋根（前頭合わせ代を含む）','noOverlap':False})
    for e in M.d['entities']:
        if e['kind']=='part' and e['visibility']=='visible' and M.colors.get(e['id'])=='透明':
            M.fitPairs.append({'first':fitted,'second':e['id'],'label':'車体・'+e['displayName'],'noOverlap':True})
    refs=M.group('12 三面図・寸法基準｜表示切替',root)
    from make_views import make_views
    make_views(args.output)
    for name,o,u,v in [('side',(0,Y+3,0),(1,0,0),(0,0,1)),('front',(-3,-25,0),(0,1,0),(0,0,1)),('top',(0,-25,51),(1,0,0),(0,1,0))]:M.image('参照 '+name,args.output/(name+'.png'),o,u,v,refs)
    M.d['activeGroupId']=root
    M.d['uiState']={'activeMode':'part','theme':'windows95','extensions':{'series115Print':{'colors':M.colors,'prototype':'Kuha115-1000 Nagano renewed','source':'design.json'}}}
    M.save(args.output/'kuha115-1000-nagano-1-80.kcd2')
    design={'scale':80,'dimensions':{'coupledLength':250,'bodyLength':L,'maximumWidth':2*Y,'roofHeight':ROOF,'floorHeight':FLOOR},'estimated':{'frontCornerRadius':3.6,'eaveHeight':EAVE,'bogieCentres':[33.4375,210.3125],'axleSpacing':26.25,'wheelDiameter':10.75,'doorCentres':DOORS,'windows':WINDOWS},'manufacturing':{'bodyWall':.85,'roofWall':.9,'floorWall':1.2,'glazingThickness':.5,'floorSideClearance':.25},'colors':M.colors,'groups':M.d['groups'],'entities':[{k:e[k] for k in ['id','kind','displayName','visibility','groupId','partProperties']} for e in M.d['entities']]}
    design['assemblyChecks']=M.fitPairs
    (args.output/'design.json').write_text(json.dumps(design,ensure_ascii=False,indent=2),encoding='utf-8')
    split_model(M,args.output,root,design)
    print(json.dumps({'entities':len(M.d['entities']),'parts':sum(e['kind']=='part' for e in M.d['entities']),'visibleParts':sum(e['kind']=='part' and e['visibility']=='visible' for e in M.d['entities']),'groups':len(M.d['groups'])}))
if __name__=='__main__':main()
