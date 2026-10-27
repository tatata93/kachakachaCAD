"""Paint guide rendered from the CAD-exported STL triangles (not an app screenshot)."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont
import json, struct, math, sys, os, io, base64
import numpy as np

PALETTE={'フォギーグレー':(224,229,219),'アルミ銀':(185,195,200),'透明':(91,132,146),
         '赤透明':(171,40,40),'黒':(35,40,42),'屋根グレー':(145,154,156),
         '床下グレー':(70,78,83),'金属銀':(164,174,179),'室内クリーム':(204,193,155),
         'アドバンスブルー':(65,106,140),'電球色':(239,226,184)}

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

def render(base,report,front=False,explode=False,camera=None,size=None,paint_names=(),
           paint_bands=None,roof_forehead_mm=None,part_ids=None,studio=False,decals=(),surface_color=None,clip_bounds=None,part_offsets=None):
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
        if part_ids is not None and part['id'] not in part_ids:continue
        colorname=part['manufacturing']['colorName'];color=PALETTE.get(colorname,PALETTE['フォギーグレー'])
        if studio and colorname=='透明':color=(188,202,200) if 'レンズ' in part['name'] else (31,57,50)
        path=part['groupPath'];paint=colorname=='フォギーグレー' and ('01 車体' in path or '客扉' in part['name'] or part['name']=='貫通扉' or any(s in part['name'] for s in paint_names))
        if roof_forehead_mm is not None and part['name']=='貫通扉':paint=False
        dz=(17 if '02 屋根' in path or '07 屋根' in path else (-13 if '08 床板' in path or '09 床下' in path or '10 台車' in path or '11 室内' in path else 0)) if explode else 0
        for poly,normal in triangles(base/part['stl']):
            if clip_bounds is not None:
                for axis in range(3):poly=clip(clip(poly,axis,clip_bounds[axis],True),axis,clip_bounds[axis+3],False)
                if len(poly)<3:continue
            if front:
                poly=clip(poly,0,36,False)
                if len(poly)<3:continue
            layers=[(poly,color)]
            if paint:
                layers=[]
                bands=paint_bands or [(-2,24.7,color),(24.7,26,(111,180,159)),(26,38.5,(65,106,140)),(38.5,60,color)]
                for low,high,col in bands:
                    q=clip(clip(poly,2,low,True),2,high,False)
                    if len(q)>=3:layers.append((q,col))
            elif roof_forehead_mm is not None and colorname=='屋根グレー' and '02 屋根' in path:
                layers=[]
                for greater,col in [(False,PALETTE['フォギーグレー']),(True,color)]:
                    q=clip(poly,0,roof_forehead_mm,greater)
                    if len(q)>=3:layers.append((q,col))
            shade=.64+.36*max(0,dot(normal,(-.4,-.5,.768)))
            specular=0
            if studio:
                shade=.36+.47*max(0,dot(normal,(-.4,-.5,.768)))+.17*max(0,dot(normal,(.65,.60,.46)))
                half=tuple(camera[i]+(-.4,-.5,.768)[i] for i in range(3));length=math.sqrt(dot(half,half));half=tuple(v/length for v in half)
                specular=(.24 if '銀' in colorname else .10)*max(0,dot(normal,half))**28
            for poly,col in layers:
                if surface_color is not None:col=surface_color(part,tuple(sum(p[i] for p in poly)/len(poly) for i in range(3)),normal,col)
                shift=(part_offsets or {}).get(part['id'],(0,0,0))
                poly=[(p[0]+shift[0],p[1]+shift[1],p[2]+dz+shift[2]) for p in poly]
                points=[(dot(p,horizontal),dot(p,up)) for p in poly]
                xs.extend(p[0] for p in points);ys.extend(p[1] for p in points)
                polygon_color=tuple(min(255,round(c*shade+255*specular)) for c in col)
                alpha=((.16 if 'レンズ' in part['name'] else .50) if colorname=='透明' else .68) if studio and colorname in ['透明','赤透明'] else 1
                polys.append(([dot(p,camera) for p in poly],points,polygon_color,alpha))
    lowx,highx,lowy,highy=min(xs),max(xs),min(ys),max(ys)
    scale=min((width-100)/(highx-lowx),(height-100)/(highy-lowy))
    ox=(width-scale*(highx-lowx))/2;oy=(height-scale*(highy-lowy))/2
    pixels=np.array(im);depth=np.full((height,width),-np.inf)
    glass_depth=np.full((height,width),-np.inf) if studio else None
    glass_pixels=np.zeros((height,width,3),dtype=np.uint8) if studio else None
    glass_alpha=np.zeros((height,width),dtype=np.float32) if studio else None
    for depths,poly,col,alpha in polys:
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
            zz=wa*za+wb*zb+wc*zc;target_depth=glass_depth if alpha<1 else depth
            target_pixels=glass_pixels if alpha<1 else pixels
            region=target_depth[ly:hy+1,lx:hx+1]
            mask=(wa>=-1e-7)&(wb>=-1e-7)&(wc>=-1e-7)&(zz>region)
            region[mask]=zz[mask];target_pixels[ly:hy+1,lx:hx+1][mask]=col
            if alpha<1:glass_alpha[ly:hy+1,lx:hx+1][mask]=alpha
    if studio:
        # Screen-space contact shading only changes illumination, never geometry.
        occupied=np.isfinite(depth);occlusion=np.zeros((height,width),dtype=np.float32)
        for distance in [2,5,9]:
            for dy,dx in [(0,distance),(0,-distance),(distance,0),(-distance,0)]:
                neighbor=np.roll(depth,(dy,dx),(0,1));valid=np.roll(occupied,(dy,dx),(0,1))&occupied
                if dy>0:valid[:dy]=False
                if dy<0:valid[dy:]=False
                if dx>0:valid[:,:dx]=False
                if dx<0:valid[:,dx:]=False
                delta=np.zeros_like(depth);np.subtract(neighbor,depth,out=delta,where=valid)
                occlusion+=valid&(delta>.08)&(delta<2.0)
        pixels=np.clip(pixels*(1-.022*occlusion[:,:,None]),0,255).astype(np.uint8)
        mask=np.isfinite(glass_depth)&(glass_depth>depth)
        alpha=glass_alpha[mask,None];pixels[mask]=(pixels[mask]*(1-alpha)+glass_pixels[mask]*alpha).astype(np.uint8)
        depth[mask]=glass_depth[mask]
    for decal in decals:
        if front and decal['origin'][0]>36:continue
        origin=decal['origin'];u=decal['u'];v=decal['v'];w=decal['widthMm'];h=decal['heightMm']
        def project(p):return np.array([ox+(dot(p,horizontal)-lowx)*scale,height-oy-(dot(p,up)-lowy)*scale])
        a=project(origin);b=project(tuple(origin[i]+u[i]*w for i in range(3)));c=project(tuple(origin[i]+v[i]*h for i in range(3)))
        matrix=np.column_stack((b-a,c-a));det=np.linalg.det(matrix)
        if abs(det)<1e-5:continue
        corners=np.array([a,b,c,b+c-a]);lx=max(0,int(np.floor(corners[:,0].min())));hx=min(width-1,int(np.ceil(corners[:,0].max())))
        ly=max(0,int(np.floor(corners[:,1].min())));hy=min(height-1,int(np.ceil(corners[:,1].max())))
        if lx>hx or ly>hy:continue
        yy,xx=np.mgrid[ly:hy+1,lx:hx+1];uv=np.linalg.inv(matrix)@np.stack((xx.ravel()+.5-a[0],yy.ravel()+.5-a[1]))
        su,sv=uv.reshape(2,hy-ly+1,hx-lx+1);zz=dot(origin,camera)+su*dot(u,camera)*w+sv*dot(v,camera)*h
        mask=(su>=0)&(su<=1)&(sv>=0)&(sv<=1)&(zz>=depth[ly:hy+1,lx:hx+1]-.005)
        texture=np.array(Image.open(io.BytesIO(base64.b64decode(decal['pngBase64']))).convert('RGBA'))
        tx=np.clip((su*texture.shape[1]).astype(int),0,texture.shape[1]-1);ty=np.clip(((1-sv)*texture.shape[0]).astype(int),0,texture.shape[0]-1)
        ink=texture[ty,tx];alpha=ink[:,:,3:4].astype(float)/255;region=pixels[ly:hy+1,lx:hx+1]
        region[mask]=(region[mask]*(1-alpha[mask])+ink[:,:,:3][mask]*alpha[mask]).astype(np.uint8)
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
