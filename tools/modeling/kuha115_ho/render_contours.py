"""Actual CAD export views and measured STL-plane sections; no concept images."""
from pathlib import Path
import argparse,json,math,os,sys
from PIL import Image,ImageDraw,ImageFont
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'series115_print'))
from render_preview import render,triangles


def section(base,parts,x,bounds):
    lines=[]
    for part in parts:
        for poly,_ in triangles(base/part['stl']):
            hit=[]
            for a,b in zip(poly,poly[1:]+poly[:1]):
                if (a[0]<=x<b[0]) or (b[0]<=x<a[0]):
                    t=(x-a[0])/(b[0]-a[0]);hit.append([a[1]+t*(b[1]-a[1]),a[2]+t*(b[2]-a[2])])
            if len(hit)==2:
                first,last=0.,1.
                for axis in range(2):
                    delta=hit[1][axis]-hit[0][axis]
                    if abs(delta)<1e-12:
                        if not bounds[axis]<=hit[0][axis]<=bounds[axis+2]:last=-1.;break
                    else:
                        ts=sorted([(bounds[axis]-hit[0][axis])/delta,(bounds[axis+2]-hit[0][axis])/delta])
                        first=max(first,ts[0]);last=min(last,ts[1])
                if first<=last:lines.append([[hit[0][k]+t*(hit[1][k]-hit[0][k]) for k in range(2)] for t in [first,last]])
    assert lines
    return lines


def door_sections(old_base,old,new_base,new,output):
    fontpath=Path(os.environ.get('WINDIR','C:/Windows'))/'Fonts/meiryo.ttc'
    if not fontpath.exists():fontpath=Path('/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc')
    font=lambda size:ImageFont.truetype(str(fontpath),size)
    im=Image.new('RGB',(1200,900),'#f4f6f7');draw=ImageDraw.Draw(im);proof={}
    draw.text((32,18),'実CADの断面比較：客扉と隣接車体の裾絞り',font=font(30),fill='#263642')
    for index,(base,report,title) in enumerate([(old_base,old,'前版：全高が平板'),(new_base,new,'修正版：下部を車体に沿わせる')]):
        ox=130+index*580;oy=795;scale=59
        parts=[p for p in report['parts'] if '運転士側客扉1葉-1' in p['name']]
        assert len(parts)==1
        body=[p for p in report['parts'] if '/01 車体' in p['groupPath']];assert len(body)==1
        shape=section(base,parts,47.,[-18.6,15.,-15.5,26.])
        adjacent=section(base,body,60.,[-18.6,15.,-15.5,26.])
        proof[title]=dict(doorX=47.,adjacentBodyX=60.,doorLines=shape,bodyLines=adjacent)
        def pixel(p):return (ox+(abs(p[0])-15.5)*scale,oy-(p[1]-15.)*scale)
        for z in range(15,27):
            draw.line([(ox,oy-(z-15)*scale),(ox+3.1*scale,oy-(z-15)*scale)],fill='#d5dbdf')
            draw.text((ox-42,oy-(z-15)*scale-10),str(z),font=font(16),fill='#63717a')
        for lines,color,width in [(adjacent,'#35926c',4),(shape,'#bd582c',4)]:
            for line in lines:draw.line([pixel(p) for p in line],fill=color,width=width)
        draw.text((ox-76,100),title,font=font(23),fill='#263642')
        draw.text((ox-60,825),'横：中心からの距離Y／縦：高さZ（mm）',font=font(17),fill='#263642')
        draw.text((ox+225,245),'緑：隣接車体\n茶：扉葉\n同じ縮尺',font=font(19),fill='#263642')
    draw.text((35,860),'STL弦誤差0.025 mm以内の断面。曲げの局所寸法は模型設計値で、実車実測値ではありません。',font=font(18),fill='#263642')
    im.save(output/'door-sections.png')
    (output/'door-sections.json').write_text(json.dumps(proof,ensure_ascii=False,indent=2),encoding='utf-8')


def main():
    ap=argparse.ArgumentParser();ap.add_argument('exports',type=Path);ap.add_argument('baseline',type=Path)
    ap.add_argument('output',type=Path);a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    read=lambda base:json.loads((base/'print-parts.json').read_text(encoding='utf-8'))
    old,new=read(a.baseline),read(a.exports);assert not old['failures'] and not new['failures']
    for prefix,base,report in [('before-',a.baseline,old),('',a.exports,new)]:
        for name,box,camera in [('cooler-side',[98,-15.5,41.8,146,15.5,51.],(0,-1,0)),
                                ('cooler-end',[98,-15.5,41.8,146,15.5,51.],(-1,-.15,.13)),
                                ('floor-apron',[-2,-18,6,8,18,15.5],(1,-.7,-.55)),
                                ('door-detail',[42.8,-18.8,15.,59.5,-15.3,38.9],(-.95,-.28,.10))]:
            def relevant(p):
                if name.startswith('cooler'):return '/02 屋根' in p['groupPath'] or 'AU75G' in p['name']
                if name=='floor-apron':return '/08 床板' in p['groupPath'] or 'スカート' in p['name']
                return '/01 車体' in p['groupPath'] or '客扉' in p['name']
            ids={p['id'] for p in report['parts'] if relevant(p)}
            def color(part,point,normal,base):
                if '/08 床板' in part['groupPath'] or '/02 屋根' in part['groupPath']:return (90,150,185)
                return (170,100,65) if 'スカート' in part['name'] else base
            render(base,report,part_ids=ids,clip_bounds=box,camera=camera,size=(1300,850),studio=True,surface_color=color).save(a.output/(prefix+name+'.png'))
            print(prefix+name,flush=True)
    door_sections(a.baseline,old,a.exports,new,a.output)


if __name__=='__main__':main()
