"""Native STL inspection views of the part changed in each later review round."""
from pathlib import Path
import argparse,hashlib,json
from render_detailed import render,wheel_paint


def main():
    ap=argparse.ArgumentParser();ap.add_argument('exports',type=Path);ap.add_argument('design',type=Path);ap.add_argument('output',type=Path)
    a=ap.parse_args();report=json.loads((a.exports/'print-parts.json').read_text(encoding='utf-8'));d=json.loads(a.design.read_text(encoding='utf-8'))
    assert not report['failures'];stage=d['refinements']['stage'];assert 6<=stage<=10
    a.output.mkdir(exist_ok=False);options=dict(studio=True,size=(1400,1000));finished=d['refinements']['finishedParts']
    if stage==6:
        views=[('pillar-driver',dict(camera=(0,-1,0),clip_bounds=[102,-20,22,138,20,40])),
               ('pillar-assistant',dict(camera=(0,1,0),clip_bounds=[102,-20,22,138,20,40]))]
    elif stage==7:
        views=[('cooler-roof',dict(camera=(-.3,-.8,.65),clip_bounds=[96,-19,39,148,19,49])),
               ('walkway-side',dict(camera=(0,-1,.15),clip_bounds=[96,-19,42,148,19,49]))]
    elif stage==8:
        views=[('roof-keys',dict(camera=(-.2,-.5,-.8),part_ids={finished['roof']},clip_bounds=[60,-20,35,100,20,47])),
               ('roof-keys-end',dict(camera=(1,0,-.2),part_ids={finished['roof']},clip_bounds=[69,-20,35,79,20,47]))]
    elif stage==9:
        views=[('floor-receivers',dict(camera=(-.2,-.5,-.8),part_ids={finished['body']},clip_bounds=[65,-19,13,85,19,20])),
               ('receiver-end',dict(camera=(1,0,-.1),part_ids={finished['body']},clip_bounds=[70,-19,13,80,19,20]))]
    else:
        views=[('tray-pads',dict(camera=(-.2,-.5,-.8),part_ids={finished['interior']},clip_bounds=[80,-16,14,108,16,21])),
               ('tray-pad-side',dict(camera=(0,-1,0),part_ids={finished['interior']},clip_bounds=[80,-16,14,108,16,21]))]
    images=[]
    for name,kw in views:
        render(a.exports,report,**options,**kw).save(a.output/(name+'.png'));images.append(name+'.png');print(name,flush=True)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    (a.output/'render-proof.json').write_text(json.dumps(dict(scope='Clipped views of the actual native-exported STL; not app screenshots',
        stage=stage,nativeReportSha256=sha(a.exports/'print-parts.json'),designSha256=sha(a.design),rendererSha256=sha(Path(__file__)),
        imagesSha256={name:sha(a.output/name) for name in images}),indent=2),encoding='utf-8',newline='\n')


if __name__=='__main__':main()
