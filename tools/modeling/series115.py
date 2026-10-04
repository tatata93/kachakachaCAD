"""115-1000 control motor car, 1:80: native editable CAD stress model.
Major envelope follows Shinano Railway. Fine dimensions are explicit approximations.
Run from the repository root: python tools/modeling/series115.py
"""
from native_kcd import Model,expr,vec
import math,json
from pathlib import Path
M=Model('115系1000番台 クモハ・重量級検証 1/80')
root=M.group('115系1000番台 1/80')
body=M.group('車体',root);front=M.group('前面',body);side=M.group('側面・窓・扉',body)
roof=M.group('屋根・屋根上機器',root);under=M.group('台枠・床下機器',root)
bogies=M.group('台車 DT21系・近似',root);inside=M.group('客室・運転室',root)
refs=M.group('製作検討・参照面',root)
L=243.75;W=36.875;Y=18.1;F=15.3125;RZ=45.675
# Roof is an analytic circular shell, not a triangulated outline.
cz=(Y*Y+39.6**2-RZ**2)/(2*(39.6-RZ));radius=RZ-cz
ang=math.asin(Y/radius)
def roof_arc(x,width=Y,top=RZ):
 c=(width*width+39.6**2-top**2)/(2*(39.6-top));r=top-c;a=math.asin(width/r)
 return M.arc((x,0,c),(1,0,0),(0,0,1),r,-a,2*a)
outer=M.arc((3,0,cz),(1,0,0),(0,0,1),radius,-ang,2*ang)
inner=M.arc((3,0,cz),(1,0,0),(0,0,1),radius-.35,ang,-2*ang)
def ap(r,a):return (3,-r*math.sin(a),cz+r*math.cos(a))
rw=M.wire('屋根 円弧断面',[outer,M.line(ap(radius,ang),ap(radius-.35,ang)),inner,M.line(ap(radius-.35,-ang),ap(radius,-ang))],roof)
M.extrude('屋根外板 厚0.35',[rw],(1,0,0),L-3,roof)
sections=[M.wire('前頭屋根 断面'+str(x),[roof_arc(x,w,t)],front) for x,w,t in [(0,17.3,44.4),(1.5,17.85,45.25),(3,Y,RZ)]]
rg=M.guide('前頭屋根 ロフト近似',sections,2,[2]*3,refs)
M.add('前頭屋根 厚0.35','part','thicken_surface',{'surface':rg,'thickness':expr(.35),'placement':2},front,[rg])
manufacturing_sections=[M.wire('屋根 製作用断面'+str(x),[roof_arc(x)],refs) for x in [3,L]]
M.guide('屋根 製作用参照面',manufacturing_sections,2,[2,2],refs)
# Side opening station positions are measured/approximated from the public side drawing.
doors=[46.25,122.5,199.5]
windows=[(29.5,10.5),(62.5,9.0375),(78,13.525),(95,13.525),(108.5,9.0375),
 (138.5,9.0375),(154,13.525),(171,13.525),(184.5,9.0375),(216,9.0375),(232,13.525)]
for sign,label in [(1,'右'),(-1,'左')]:
 g=M.group(label+'側',side);y=sign*Y;n=(0,-sign,0)
 outer=M.rect(label+'側外板 輪郭',(3,y,14.3),(1,0,0),(0,0,1),L-3,25.3,g)
 holes=[M.rounded(label+'運転室側窓 開口',(5,y,29.5),(1,0,0),(0,0,1),7.5,8.1,.55,g)]
 for i,(x,w) in enumerate(windows):
  holes.append(M.rounded(f'{label}窓{i+1} 開口',(x-w/2,y,27),(1,0,0),(0,0,1),w,10.6,.65,g))
 for i,x in enumerate(doors):holes.append(M.rounded(f'{label}客扉{i+1} 開口',(x-8.125,y,F),(1,0,0),(0,0,1),16.25,23.0,.45,g))
 holes.append(M.rounded(label+'乗務員扉 開口',(14,y,F),(1,0,0),(0,0,1),7.8,22.5,.55,g))
 M.extrude(label+'側外板 窓・扉開口済',[outer]+holes,n,.35,g)
 bevel=M.poly(label+'前面絞り外板輪郭',[(0,sign*17.3,14.3),(3,y,14.3),(3,y,39.6),(0,sign*17.3,39.6)],g)
 M.extrude(label+'前面絞り外板',[bevel],n,.35,g)
 co=M.rounded('運転室側窓ゴム外',(4.8,y+sign*.08,29.3),(1,0,0),(0,0,1),7.9,8.5,.7,g)
 ci=M.rounded('運転室側窓ゴム内',(5.2,y+sign*.08,29.7),(1,0,0),(0,0,1),7.1,7.7,.4,g)
 M.extrude('運転室側窓ゴム',[co,ci],n,.18,g)
 for i,(x,w) in enumerate(windows):
  wg=M.group(f'窓{i+1:02d}',g)
  a=M.rounded('窓枠 外周',(x-w/2-.18,y+sign*.08,26.82),(1,0,0),(0,0,1),w+.36,10.96,.75,wg)
  b=M.rounded('窓枠 内周',(x-w/2+.25,y+sign*.08,27.25),(1,0,0),(0,0,1),w-.5,10.1,.55,wg)
  M.extrude('窓サッシ',[a,b],n,.18,wg)
  # Open glazing apertures keep the modeled interior visible.
  if w>10:
   M.box('上下窓 中桟',x-w/2,y-.10,32.1,w,.20,.23,wg)
   for dx in [-w/2+.35,w/2-.8]:M.box('窓開閉つまみ',x+dx,y-sign*.15,31.75,.45,.28,.5,wg)
 for i,x in enumerate(doors):
  dg=M.group(f'客扉{i+1}',g)
  for half in [-1,1]:
   x0=x-7.98 if half<0 else x+.08
   o=M.rounded('扉 外周',(x0,y-sign*.35,F+.05),(1,0,0),(0,0,1),7.9,22.8,.4,dg)
   h=M.rounded('扉窓 開口',(x0+1.35,y-sign*.35,28.4),(1,0,0),(0,0,1),5.15,8.8,.65,dg)
   M.extrude('客扉 左葉' if half<0 else '客扉 右葉',[o,h],n,.28,dg)
   M.box('扉下ガイド',x0,y-sign*.5,F+.5,7.9,.4,.25,dg)
   M.box('戸当たり',x+(half*.1),y-sign*.12,F+.2,.12,.20,22.1,dg)
  M.box('扉ステップ',x-8.2,y-1.2625 if sign>0 else y-.3375,F-.35,16.4,1.6,.28,dg)
 dg=M.group('乗務員扉',g)
 o=M.rounded('乗務員扉外形',(14.1,y-sign*.4,F+.1),(1,0,0),(0,0,1),7.6,22.3,.5,dg)
 h=M.rounded('乗務員窓',(15.2,y-sign*.4,29.5),(1,0,0),(0,0,1),5.4,7.1,.5,dg)
 M.extrude('乗務員扉',[o,h],n,.3,dg)
 M.box('乗務員取手',20.9,y+sign*.06-.12,25,.28,.24,2.0,dg)
 for x in [12.8,22.6]:M.tube('乗務員手すり',(x,y+sign*.19,22),(x,y+sign*.19,29),.14,dg)
 M.box('雨樋',3,y-.12,39.6,L-3,.24,.23,g)
 M.box('裾帯',3,y-.10,14.6,L-3,.20,.38,g)
 # Long roof-side seam, separate editable wire retained hidden.
# Front plate with actual openings, inset door and annular lamps.
fg=M.group('前面外板・窓',front)
outline=M.wire('前面外形',[M.line((0,-17.3,14.3),(0,17.3,14.3)),M.line((0,17.3,14.3),(0,17.3,39.6)),roof_arc(0,17.3,44.4),M.line((0,-17.3,39.6),(0,-17.3,14.3))],fg)
fholes=[]
for sy in [-1,1]:
 y0=-15.7 if sy<0 else 5.5
 fholes.append(M.rounded('前面窓 開口',(0,y0,29.1),(0,1,0),(0,0,1),10.2,9.8,.9,fg))
 o=M.rounded('前面窓ゴム外',(-.15,y0-.25,28.85),(0,1,0),(0,0,1),10.7,10.3,1.1,fg)
 h=M.rounded('前面窓ゴム内',(-.15,y0+.15,29.25),(0,1,0),(0,0,1),9.9,9.5,.8,fg)
 M.extrude('前面窓ゴム',[o,h],(1,0,0),.18,fg)
 M.tube('ワイパーアーム',(-.4,y0+5,29.4),(-.4,y0+2.3,34),.10,fg)
 M.tube('ワイパーブレード',(-.5,y0+1.1,33.7),(-.5,y0+4.1,35.4),.12,fg)
fholes.append(M.rounded('貫通扉開口',(0,-4.1,F),(0,1,0),(0,0,1),8.2,23.1,.8,fg))
M.extrude('前面外板 開口済',[outline]+fholes,(1,0,0),.45,fg)
o=M.rounded('貫通扉外',(-.1,-3.9,F+.15),(0,1,0),(0,0,1),7.8,22.7,.7,fg)
h=M.rounded('貫通扉窓',(-.1,-2.65,29),(0,1,0),(0,0,1),5.3,8.3,.6,fg)
M.extrude('貫通扉',[o,h],(1,0,0),.30,fg)
M.box('貫通扉取手',-.42,2.7,25,.4,.25,1.3,fg)
M.box('前面渡り板',-1.5,-4.3,F-.25,1.6,8.6,.3,fg)
M.box('方向幕ケース',-.25,-4.5,40,.6,9,2.35,fg)
for sy in [-1,1]:
 lg=M.group('ライト '+('右' if sy>0 else '左'),front);y=sy*12.2
 for label,z,r in [('前照灯',25.5,1.85),('尾灯',20.3,1.0)]:
  M.ring(label+'リム',(-.8,y,z),(1,0,0),r,r-.22,.9,lg)
  M.cylinder(label+'レンズ',(-.85,y,z),(1,0,0),r-.25,.12,lg)
 M.tube('前面手すり',(-.55,sy*6,22),(-.55,sy*10.2,22),.13,lg)
# Rear gangway end.
eg=M.group('妻面・幌',body)
p=M.rect('妻外板',(L-.35,-Y,14.3),(0,1,0),(0,0,1),2*Y,25.3,eg)
h=M.rounded('妻貫通口',(L-.35,-4.1,F),(0,1,0),(0,0,1),8.2,23,.6,eg)
M.extrude('妻外板',[p,h],(1,0,0),.35,eg)
p=M.wire('妻屋根断面',[roof_arc(L-.35),M.line((L-.35,-Y,39.6),(L-.35,Y,39.6))],eg)
M.extrude('妻屋根塞ぎ板',[p],(1,0,0),.35,eg)
for i in range(5):
 o=M.rounded('幌外輪',(L+i*.35,-4.7,F-.1),(0,1,0),(0,0,1),9.4,23.8,.8,eg)
 h=M.rounded('幌内輪',(L+i*.35,-4,F+.45),(0,1,0),(0,0,1),8,22.6,.6,eg)
 M.extrude('幌 蛇腹'+str(i+1),[o,h],(1,0,0),.18,eg)
# Finish in separate detail modules to keep the model readable.
if __name__=='__main__':
 import series115_detail
 series115_detail.build(M,roof,under,bogies,inside,L,F)
 out=Path('samples/series115-heavy/series115-1000-1-80.kcd2');M.save(out)
 counts={k:sum(e['kind']==k for e in M.d['entities']) for k in ['wire','guide_surface','part','work_plane']}
 print(json.dumps({'file':str(out),'features':len(M.d['features']),'groups':len(M.d['groups']),**counts},ensure_ascii=False))
