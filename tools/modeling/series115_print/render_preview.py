"""Paint guide rendered from the CAD-exported STL triangles (not an app screenshot)."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import json, struct, math, sys, os
import numpy as np

PALETTE={'フォギーグレー':(224,229,219),'アルミ銀':(185,195,200),'透明':(91,132,146),
         '赤透明':(171,40,40),'黒':(35,40,42),'屋根グレー':(145,154,156),
         '床下グレー':(70,78,83),'金属銀':(164,174,179),'室内クリーム':(204,193,155),
         'アドバンスブルー':(65,106,140)}

def triangles(path):
    data=path.read_bytes();count=struct.unpack_from('<I',data,80)[0]
    assert len(data)==84+count*50
    for i in range(count):
        raw=struct.unpack_from('<12fH',data,84+50*i)
        yield [raw[3:6],raw[6:9],raw[9:12]],raw[:3]

def clip(poly,axis,bound,greater):
    output=[]
    for a,b in zip(poly,poly[1:]+poly[:1]):
        ia=(a[axis]>=bound) if greater else (a[axis]<=bound)
        ib=(b[axis]>=bound) if greater else (b[axis]<=bound)
        if ia:output.append(a)
        if ia!=ib:
            t=(bound-a[axis])/(b[axis]-a[axis])
            output.append(tuple(a[j]+(b[j]-a[j])*t for j in range(3)))
    return output

def render(base,report,front=False,explode=False,camera=None,size=None,paint_names=()):
    width,height=size or ((1300,1100) if front else (2600,1050))
    im=Image.new('RGB',(width,height),'#edf1f2');draw=ImageDraw.Draw(im)
    camera=camera or ((-.91,-.32,.26) if front else (-.35,-.75,.56))
    length=math.sqrt(sum(v*v for v in camera));camera=tuple(v/length for v in camera)
    horizontal=(-camera[1],camera[0],0);length=math.hypot(*horizontal[:2])
    horizontal=tuple(v/length for v in horizontal) if length>1e-12 else (1,0,0)
    up=(-camera[2]*horizontal[1],camera[2]*horizontal[0],camera[0]*horizontal[1]-camera[1]*horizontal[0])
    def dot(a,b):return sum(x*y for x,y in zip(a,b))
    polys=[];xs=[];ys=[]
    for part in report['parts']:
        colorname=part['manufacturing']['colorName'];color=PALETTE.get(colorname,PALETTE['フォギーグレー'])
        path=part['groupPath'];paint=colorname=='フォギーグレー' and ('01 車体' in path or '客扉' in part['name'] or part['name']=='貫通扉' or any(s in part['name'] for s in paint_names))
        dz=(17 if '02 屋根' in path or '07 屋根' in path else (-13 if '08 床板' in path or '09 床下' in path or '10 台車' in path or '11 室内' in path else 0)) if explode else 0
        for poly,normal in triangles(base/part['stl']):
            if front:
                poly=clip(poly,0,36,False)
                if len(poly)<3:continue
            layers=[(poly,color)]
            if paint:
                layers=[]
                for low,high,col in [(-2,24.7,color),(24.7,26,(111,180,159)),(26,38.5,(65,106,140)),(38.5,60,color)]:
                    q=clip(clip(poly,2,low,True),2,high,False)
                    if len(q)>=3:layers.append((q,col))
            shade=.64+.36*max(0,dot(normal,(-.4,-.5,.768)))
            for poly,col in layers:
                poly=[(p[0],p[1],p[2]+dz) for p in poly]
                points=[(dot(p,horizontal),dot(p,up)) for p in poly]
                xs.extend(p[0] for p in points);ys.extend(p[1] for p in points)
                polys.append(([dot(p,camera) for p in poly],points,tuple(round(c*shade) for c in col)))
    lowx,highx,lowy,highy=min(xs),max(xs),min(ys),max(ys)
    scale=min((width-100)/(highx-lowx),(height-100)/(highy-lowy))
    ox=(width-scale*(highx-lowx))/2;oy=(height-scale*(highy-lowy))/2
    pixels=np.array(im);depth=np.full((height,width),-np.inf)
    for depths,poly,col in polys:
        xy=[(ox+(x-lowx)*scale,height-oy-(y-lowy)*scale) for x,y in poly]
        for i in range(1,len(xy)-1):
            a,b,c=xy[0],xy[i],xy[i+1];za,zb,zc=depths[0],depths[i],depths[i+1]
            den=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
            if abs(den)<1e-10:continue
            lx=max(0,math.floor(min(a[0],b[0],c[0])));hx=min(width-1,math.ceil(max(a[0],b[0],c[0])))
            ly=max(0,math.floor(min(a[1],b[1],c[1])));hy=min(height-1,math.ceil(max(a[1],b[1],c[1])))
            if hx<lx or hy<ly:continue
            yy,xx=np.mgrid[ly:hy+1,lx:hx+1];xx=xx+.5;yy=yy+.5
            wa=((b[1]-c[1])*(xx-c[0])+(c[0]-b[0])*(yy-c[1]))/den
            wb=((c[1]-a[1])*(xx-c[0])+(a[0]-c[0])*(yy-c[1]))/den;wc=1-wa-wb
            zz=wa*za+wb*zb+wc*zc;region=depth[ly:hy+1,lx:hx+1]
            mask=(wa>=-1e-7)&(wb>=-1e-7)&(wc>=-1e-7)&(zz>region)
            region[mask]=zz[mask];pixels[ly:hy+1,lx:hx+1][mask]=col
    return Image.fromarray(pixels)

def main():
    base=Path(sys.argv[1]);out=Path(sys.argv[2]);out.mkdir(parents=True,exist_ok=True)
    report=json.loads((base/'print-parts.json').read_text(encoding='utf-8'))
    fontpath=Path(os.environ.get('WINDIR','/nonexistent'))/'Fonts/meiryo.ttc'
    font=ImageFont.truetype(str(fontpath),27) if fontpath.exists() else ImageFont.load_default()
    im=render(base,report);front=render(base,report,front=True);exploded=render(base,report,explode=True)
    sheet=Image.new('RGB',(2700,2690),'white');d=ImageDraw.Draw(sheet)
    d.text((50,20),'クハ115-1000 長野色｜1/80｜先頭車・リニューアル後の代表形',font=font,fill='#203544')
    sheet.paste(im,(50,70));sheet.paste(front.resize((1100,930)),(50,1180))
    sheet.paste(exploded.resize((1430,578)),(1200,1220))
    for y,text in [(1830,'CADで再生成・検査したSTL形状から作成した塗装・分解プレビュー'),
                   (2160,'車体・屋根・床板・ガラス・サッシ・灯具・台車・床下・室内を別グループに収録'),
                   (2230,'車体長243.750 / 幅36.875 / 屋根高45.675 mm（機器を除く）'),
                   (2300,'黒Hゴム、側面サッシ、中桟、前面へ回り込む窓、ドーム状の屋根を立体化'),
                   (2370,'局所形状・窓配置・色のRGB値は推定。特定車番の実測精密模型ではありません。'),
                   (2440,'台車は展示用一体構造。実機印刷・走行・市販車輪との互換試験は未実施。'),
                   (2510,'この図は塗装説明用レンダリングです。実際のCAD画面は cad-screen.png を参照。')]:
        d.text((1200 if y<1900 else 60,y),text,font=font,fill='#253443')
    sheet.save(out/'model-preview.png')
    im.save(out/'painted-overview.png');front.save(out/'front-geometry.png');exploded.save(out/'exploded.png')

if __name__=='__main__':main()
