"""Fine parts for the 115 native-feature model. Photo-based dimensions are approximations."""
import math

def build(M,roof,under,bogies,inside,L,F):
 roof_parts(M,roof)
 under_parts(M,under,L,F)
 for x,name in [(35.625,'前台車'),(208.125,'後台車')]:bogie(M,x,M.group(name,bogies))
 interior(M,inside,F)

def roof_parts(M,parent):
 g=M.group('AU75形 集中冷房装置・近似',parent)
 M.box('クーラー台座',99,-11.3,45.1,44,22.6,.7,g)
 M.box('クーラー筐体',99.5,-10.8,45.8,43,21.6,3.0,g)
 M.box('天蓋',99.2,-11,48.8,43.6,22,.25,g)
 for x in [106,118,130,138]:
  M.ring('送風機保護枠',(x,0,49.05),(0,0,1),3.5,3.2,.23,g)
  M.cylinder('送風機ハブ',(x,0,49.05),(0,0,1),.6,.35,g)
  for a in range(0,180,30):
   t=math.radians(a);dx,d=3.1*math.cos(t),3.1*math.sin(t)
   M.tube('送風機保護格子',(x-dx,-d,49.4),(x+dx,d,49.4),.07,g)
 for sign in [-1,1]:
  for i in range(28):M.box('クーラー側面ルーバー',100.2+i*1.49,sign*10.9-.07,46.15,.25,.14,2.2,g)
 for x in [101,141]:
  for y in [-9,9]:M.cylinder('クーラー締結ボルト',(x,y,49.05),(0,0,1),.17,.16,g)
 for i,x in enumerate([27,65,82,167,193,221]):
  v=M.group('押込通風器 '+str(i+1),parent)
  M.box('通風器基台',x-3.9,-3.3,45.25,7.8,6.6,.5,v)
  section=M.poly('通風器断面',[(x-3.7,-3,45.75),(x+3.7,-3,45.75),(x+3,-3,47),(x-3,-3,47)],v)
  M.extrude('通風器カバー',[section],(0,1,0),6,v)
  for j in range(7):M.box('通風器スリット',x-2.9+j*.9,-3.08,46,.22,.16,.65,v)
 g=M.group('信号炎管・アンテナ・ホイッスル',parent)
 M.cylinder('信号炎管',(7,-6,44.8),(0,0,1),.43,2.3,g)
 M.cylinder('列車無線アンテナ基部',(13,0,45.65),(0,0,1),.65,.6,g)
 M.cylinder('列車無線アンテナ',(13,0,46.25),(0,0,1),.21,3.1,g)
 M.cylinder('ホイッスル',(6,6,44.8),(0,0,1),.52,1.1,g)
 M.box('ホイッスルカバー',4.9,5.15,45.8,2.2,1.7,.45,g)

def under_parts(M,parent,L,F):
 frame=M.group('台枠・床板',parent)
 M.box('床板',.5,-17.7,F-.7,L-1,35.4,.7,frame)
 for y in [-16.3,15.3]:M.box('側梁',2,y,12.8,L-4,1,1.8,frame)
 for x in range(12,240,16):M.box('横梁',x,-16,12.9,.8,32,1.4,frame)
 g=M.group('主制御器・抵抗器・電装箱 近似',parent)
 # Mc car equipment families; sizes/placement are modeling approximations, not an as-built equipment map.
 for i,(x,y,a,b,c) in enumerate([(67,-15,20,10,5),(91,-15,30,11,5),(126,-15,24,10,4.5),(156,-15,23,10,5),
   (69,5,25,10,5.2),(99,5,22,10,4.8),(128,5,18,10,5),(153,5,27,10,4.8)]):
  box=M.group('機器箱 '+str(i+1),g)
  M.box('機器筐体',x,y,8.2,a,b,c,box)
  sy=y-.12 if y<0 else y+b
  M.box('機器蓋',x+.25,sy,8.5,a-.5,.2,c-.6,box)
  for bx in [x+1,x+a-1]:
   M.box('吊り金具',bx,y+1,8.2+c,.7,b-2,1.6,box)
   for bz in [9,8.2+c-.7]:M.cylinder('蓋ボルト',(bx,sy-.1,bz),(0,1,0),.12,.22,box)
  for j in range(int(a/1.3)):M.box('機器通風ルーバー',x+.8+j*1.25,sy-.08,9,.19,.15,c-1.6,box)
 for i,(x,y) in enumerate([(60,-7),(182,-6),(185,7)]):
  t=M.group('空気溜 '+str(i+1)+' 近似',parent)
  M.cylinder('空気溜胴',(x,y,10.3),(1,0,0),2.1,12,t)
  for bx in [x,x+11.5]:M.cylinder('空気溜鏡板',(bx,y,10.3),(1,0,0),2.0,.5,t)
  for bx in [x+2,x+9]:M.ring('空気溜吊り帯',(bx,y,10.3),(1,0,0),2.25,2.1,.45,t)
 p=M.group('床下配管・配線 近似',parent)
 for y in [-3.5,0,3.5]:M.tube('幹線',(8,y,12.6),(L-8,y,12.6),.15,p)
 for x in [60,90,125,155,183]:
  for y in [-10,10]:M.tube('分岐配管',(x,0,12.6),(x,y,12.6),.12,p)
 for x,sign in [(0,-1),(L,1)]:
  g=M.group('前連結器・スカート' if sign<0 else '後連結器',parent)
  M.box('連結器胴',x-1.4,-1.1,9,2.8,2.2,1.9,g)
  M.box('密着連結器頭',-3.125 if sign<0 else L+2.325,-1.5,9, .8,3,2,g)
  M.tube('連結器支持腕',(x,0,10),(x-sign*7,0,11),.65,g)
  if sign<0:
   for s in [-1,1]:
    p=M.poly('スカート輪郭',[(-.8,s*2.4,8),(-.8,s*15.7,8),(-.8,s*16.5,13.7),(-.8,s*2.4,13.7)],g)
    M.extrude('スカート',[p],(1,0,0),.45,g)
    for y in [s*5,s*8]:M.tube('ジャンパ栓ホース',(-1.1,y,14),(-1.8,y+s*1.0,10),.22,g)

def bogie(M,x,g):
 # 2100mm axle spacing / 860mm wheel diameter at 1:80. Detail radii are approximations.
 for side in [-1,1]:
  y=side*13.4;sg=M.group('台車側枠 '+str(side),g)
  p=M.poly('側枠断面',[(x-15,y,8.4),(x-12,y,10.1),(x-5,y,9.3),(x+5,y,9.3),(x+12,y,10.1),(x+15,y,8.4),
    (x+15,y,6.6),(x+8,y,6.3),(x+5,y,7.3),(x-5,y,7.3),(x-8,y,6.3),(x-15,y,6.6)],sg)
  M.extrude('DT21系側枠 近似',[p],(0,-side,0),1.3,sg)
  for ax in [-13.125,13.125]:
   M.box('軸箱',x+ax-1.5,y-1.2,4.1,3,2.4,3.1,sg)
   M.cylinder('軸受蓋',(x+ax,y+side*1.3,5.5),(0,-side,0),1.05,.25,sg)
   for b in range(4):
    t=math.pi/4+b*math.pi/2
    M.cylinder('軸受蓋ボルト',(x+ax+.7*math.cos(t),y+side*1.4,5.5+.7*math.sin(t)),(0,-side,0),.13,.2,sg)
   for sx in [-2.4,2.4]:
    for k in range(5):M.ring('軸ばね巻き 近似',(x+ax+sx,y,7.1+k*.35),(0,0,1),.65,.43,.18,sg)
  for ax in [-4,4]:
   for k in range(7):M.ring('枕ばね巻き 近似',(x+ax,y-side*1.0,9.4+k*.32),(0,0,1),1.1,.8,.19,sg)
  M.tube('ブレーキ連結棒',(x-14,y-side*2,5),(x+14,y-side*2,5),.18,sg)
 M.box('枕梁',x-2.0,-12.5,10.1,4,25,1.5,g)
 for ax in [-13.125,13.125]:
  a=x+ax;wg=M.group('輪軸 '+str(ax),g)
  M.cylinder('車軸',(a,-12,5.375),(0,1,0),.75,24,wg)
  for s in [-1,1]:
   y=-10.2 if s<0 else 8.25
   M.cylinder('車輪踏面',(a,y,5.375),(0,1,0),5.375,1.95,wg)
   M.cylinder('フランジ',(a,-8.5 if s<0 else 8.25,5.375),(0,1,0),5.7,.28,wg)
   M.cylinder('車輪ハブ',(a,-10.5 if s<0 else 10.2,5.375),(0,1,0),1.75,.3,wg)
   for d in [-1,1]:M.box('制輪子',a+d*5.3-.3,s*8.9-.7,4.2,.6,1.4,2.4,wg)
  mg=M.group('主電動機・歯車箱 近似',wg)
  M.cylinder('主電動機',(a+3,-6,7.2),(0,1,0),2.2,12,mg)
  M.cylinder('歯車箱',(a,-5.8,5.6),(0,1,0),2.1,2,mg)

def interior(M,parent,F):
 seats=M.group('セミクロスシート 近似',parent)
 for center in [76,95,153,172]:
  for direction in [-1,1]:
   x=center+direction*4.5
   for side in [-1,1]:
    g=M.group('ボックス座席 '+str(x)+' '+str(side),seats);y=side*11.7-4.5
    M.box('座面',x-2.9,y,F+3.6,5.8,9,1,g)
    M.box('背もたれ',x+direction*2.7-.35,y,F+4.6,.7,9,6.3,g)
    for yy in [y+.6,y+7.8]:M.box('座席脚',x-.35,yy,F,.7,.65,3.6,g)
    M.tube('背面取手',(x+direction*3.3,y+.8,F+10.6),(x+direction*3.3,y+8.2,F+10.6),.12,g)
 for x in [30,61,111,138,188,216,232]:
  for s in [-1,1]:
   y=-17.2 if s<0 else 11.7
   M.box('ロングシート座面',x-3.8,y,F+3.6,7.6,5.5,.9,seats)
   M.box('ロングシート背',x-3.8,-17.5 if s<0 else 16.7,F+4.5,7.6,.65,5.5,seats)
 g=M.group('つり革・荷棚 近似',parent)
 for s in [-1,1]:
  M.tube('つり革棒',(27,s*7.8,37.5),(239,s*7.8,37.5),.14,g)
  for x in range(30,240,9):
   M.tube('つり革ベルト',(x,s*7.8,37.5),(x,s*7.8,35.6),.10,g)
   M.ring('つり革輪',(x,s*7.8-.1,34.9),(0,1,0),.65,.47,.20,g)
  M.box('荷棚縁',28,s*12.6,36.5,210,.25,.3,g)
  for x in range(30,240,8):M.tube('荷棚受け',(x,s*12.6,36.5),(x,s*17.3,36.5),.12,g)
 cab=M.group('運転室 近似',parent)
 M.box('運転室仕切り',22.8,-17.7,F,.35,35.4,23.5,cab)
 M.box('運転台',2,-15.5,F+7,6.4,13.5,3.1,cab)
 M.box('助手側機器台',2,9,F+5,5,7,4,cab)
 M.cylinder('運転席支柱',(8,-10,F),(0,0,1),.6,5,cab)
 M.box('運転席座面',5.5,-12.5,F+5,5,5,.8,cab)
 M.box('運転席背もたれ',10,-12.5,F+5.8,.6,5,5,cab)
 for y in [-13,-10,-7]:M.cylinder('計器 近似',(4,y,F+10.1),(0,0,1),.7,.2,cab)
 M.tube('ブレーキハンドル',(5,-4,F+10.2),(5,-4,F+12),.14,cab)
