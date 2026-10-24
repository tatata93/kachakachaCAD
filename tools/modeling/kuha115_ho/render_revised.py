"""Generate views solely from CAD-evaluated, exported revised model triangles."""
from pathlib import Path
import argparse,json,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'series115_print'))
from render_preview import render


def main():
    ap=argparse.ArgumentParser();ap.add_argument('exports',type=Path);ap.add_argument('output',type=Path)
    a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    report=json.loads((a.exports/'print-parts.json').read_text(encoding='utf-8'))
    assert report['parts'] and not report['failures']
    options=dict(paint_names=('乗務員扉','シャッター','前面横手掛け'),roof_forehead_mm=8.5)
    options['paint_bands']=[(-2,24.4,(224,229,219)),(24.4,26.0,(90,174,151)),
                            (26.0,38.5,(110,154,184)),(38.5,60,(224,229,219))]
    for name,kw in [('overview',dict(camera=(-.38,-.88,.28),size=(2600,1000))),
                    ('front-detail',dict(front=True,camera=(-.94,-.26,.17),size=(1400,1300))),
                    ('front',dict(front=True,camera=(-1,0,0),size=(1000,1300))),
                    ('driver-side',dict(camera=(0,-1,0),size=(2700,650))),
                    ('assistant-side',dict(camera=(0,1,0),size=(2700,650))),
                    ('top',dict(camera=(0,0,1),size=(2700,650))),
                    ('exploded',dict(explode=True,size=(2600,1400)))]:
        render(a.exports,report,**options,**kw).save(a.output/(name+'.png'))
        print(name,flush=True)
    ids={p['id'] for p in report['parts'] if '/10 台車' in p['groupPath'] and '/前台車' in p['groupPath']}
    assert ids
    render(a.exports,report,camera=(-.4,-.9,.35),size=(1400,950),part_ids=ids).save(a.output/'bogie.png')


if __name__=='__main__':main()
