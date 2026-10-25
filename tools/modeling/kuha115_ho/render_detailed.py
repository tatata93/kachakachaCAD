"""Render the actual detailed CAD mesh with glazing, paint decals and studio light."""
from pathlib import Path
import argparse, json, math, sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'series115_print'))
from render_preview import render


def wheel_paint(part,point,normal,color):
    if '/11 室内' in part['groupPath']:return tuple(round(c*.35) for c in color)
    if '輪軸・内側二枚ディスク' not in part['name']:return color
    x=min([35.625+d for d in [-13.125,13.125]]+[208.125+d for d in [-13.125,13.125]],key=lambda q:abs(point[0]-q))
    radius=math.hypot(point[0]-x,point[2]-5.375)
    if abs(point[1])<6:return (111,115,113) if radius>1.5 else (71,77,78)
    return (157,164,164) if radius>5.05 else (49,55,56)


def main():
    ap=argparse.ArgumentParser();ap.add_argument('exports',type=Path);ap.add_argument('design',type=Path)
    ap.add_argument('output',type=Path);ap.add_argument('--diagnostic-front',action='store_true');a=ap.parse_args()
    report=json.loads((a.exports/'print-parts.json').read_text(encoding='utf-8'));design=json.loads(a.design.read_text(encoding='utf-8'))
    assert report['parts'] and (a.diagnostic_front or not report['failures'])
    a.output.mkdir(parents=True,exist_ok=True)
    options=dict(paint_names=('乗務員扉','シャッター','前面横手掛け'),roof_forehead_mm=8.5,
        paint_bands=[(-2,22.4,(224,229,219)),(22.4,24.0,(90,174,151)),(24.0,38.5,(110,154,184)),(38.5,60,(224,229,219))],
        studio=True,decals=design.get('paintDecals',[]),surface_color=wheel_paint)
    views=[('overview',dict(camera=(-.38,-.88,.28),size=(2600,1000))),
           ('front-detail',dict(front=True,camera=(-.94,-.26,.17),size=(1400,1300))),
           ('front',dict(front=True,camera=(-1,0,0),size=(1000,1300))),
           ('front-profile',dict(front=True,camera=(0,-1,0),size=(1100,1300))),
           ('driver-side',dict(camera=(0,-1,0),size=(2700,650))),
           ('assistant-side',dict(camera=(0,1,0),size=(2700,650))),
           ('top',dict(camera=(0,0,1),size=(2700,650))),('exploded',dict(explode=True,size=(2600,1400)))]
    if a.diagnostic_front:views=views[1:4]
    for name,kw in views:
        render(a.exports,report,**options,**kw).save(a.output/(name+'.png'));print(name,flush=True)
    if not a.diagnostic_front:
        ids={p['id'] for p in report['parts'] if '/10 台車' in p['groupPath'] and '/前台車' in p['groupPath']};assert ids
        render(a.exports,report,camera=(-.4,-.9,.35),size=(1400,950),part_ids=ids,studio=True,surface_color=wheel_paint).save(a.output/'bogie.png')


if __name__=='__main__':main()
