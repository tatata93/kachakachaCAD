"""Assembly views made from actual exported native parts; no invented geometry."""
from pathlib import Path
import argparse,hashlib,json
from render_detailed import render


def main():
    ap=argparse.ArgumentParser();ap.add_argument('exports',type=Path);ap.add_argument('design',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
    r=json.loads((a.exports/'print-parts.json').read_text(encoding='utf-8'));d=json.loads(a.design.read_text(encoding='utf-8'))
    assert not r['failures'] and d['kit'];a.output.mkdir(exist_ok=False);panels=d['kit']['panels'];ids=set(panels.values())
    shifts={}
    for key,ident in panels.items():
        shifts[ident]=(-18,0,0) if key=='front' else (18,0,0) if key=='rear' else (0,-18,0) if key.startswith('driver') else (0,18,0) if key.startswith('assistant') else (0,0,20) if key.startswith('roof') else (0,0,-15) if key.startswith('floor') else (0,0,-5)
        if key.endswith(('Front','Rear')):
            old=shifts[ident];shifts[ident]=(old[0]+(-7 if key.endswith('Front') else 7),old[1],old[2])
    films=set(d['kit']['sideGlazing']);film_offsets={}
    if d['kit']['compact']:
        film_offsets={p['id']:(-7 if (p['bounds'][0]+p['bounds'][3])/2<117.8 else 7,0,0) for p in r['parts'] if p['id'] in films}
    views=[('panels',dict(camera=(-.3,-.85,.45),part_ids=ids,part_offsets=shifts,size=(2400,1500))),
        ('front-panel',dict(camera=(-.7,-.6,.35),part_ids={panels['front']},size=(1350,1100))),
        ('assembled-shell',dict(camera=(-.35,-.85,.25),part_ids=ids|{d['kit']['climateUnit']},size=(2400,950))),
        ('glazing',dict(camera=(-.25,-.9,.35),part_ids=films,part_offsets=film_offsets,size=(2400,850)))]
    if d['kit']['backers']:
        offsets=dict(zip(d['kit']['backers'],[(-12,0,0),(12,0,0),(0,0,8),(0,-6,0),(0,6,0)]))
        views.append(('backers',dict(camera=(-.2,-.6,.6),part_ids=set(d['kit']['backers']),part_offsets=offsets,size=(1600,1100))))
    written=[]
    for name,kw in views:
        render(a.exports,r,studio=True,**kw).save(a.output/(name+'.png'));written.append(name+'.png');print(name,flush=True)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    (a.output/'render-proof.json').write_text(json.dumps(dict(scope='Native-exported STL with explicitly recorded display-only translations; not app screenshots',
        nativeReportSha256=sha(a.exports/'print-parts.json'),designSha256=sha(a.design),offsetsMm=shifts,
        offsetsByImage={name+'.png':kw.get('part_offsets',{}) for name,kw in views},
        imagesSha256={name:sha(a.output/name) for name in written}),indent=2),encoding='utf-8')


if __name__=='__main__':main()
