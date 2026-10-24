"""Views of verified CAD-exported triangles; these are renders, not app screenshots."""
from pathlib import Path
import argparse, json, sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'series115_print'))
from render_preview import render


def main():
    ap=argparse.ArgumentParser();ap.add_argument('exports',type=Path);ap.add_argument('output',type=Path)
    a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    report=json.loads((a.exports/'print-parts.json').read_text(encoding='utf-8'))
    assert not report['failures'] and report['visibleParts']>0
    options=dict(paint_names=('乗務員扉',))
    for name,kw in [('overview',{}),('front-detail',dict(front=True)),('exploded',dict(explode=True)),
                    ('side',dict(camera=(0,-1,0),size=(2600,650))),
                    ('front',dict(camera=(-1,0,0),front=True,size=(900,1100))),
                    ('top',dict(camera=(0,0,1),size=(2600,650)))]:
        render(a.exports,report,**kw,**options).save(a.output/(name+'.png'))
        print(name,flush=True)


if __name__=='__main__':main()
