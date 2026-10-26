"""Detail views of actual attachment geometry; clipped render, not an AI concept."""
from pathlib import Path
import argparse,json,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'series115_print'))
from render_preview import render


def main():
    ap=argparse.ArgumentParser();ap.add_argument('exports',type=Path);ap.add_argument('output',type=Path);a=ap.parse_args()
    report=json.loads((a.exports/'print-parts.json').read_text(encoding='utf-8'));assert report['parts'] and not report['failures']
    a.output.mkdir(parents=True,exist_ok=True)
    for name,groups,box,camera in [
        ('cooler-seats',('02 屋根','07 屋根'),[99,-15,42,146,15,51],(-.5,-1,.15)),
        ('skirt-arms',('08 床板','06 前頭部'),[-2,-18,6,8,18,15],(1,-.7,-.55))]:
        ids={p['id'] for p in report['parts'] if any(g in p['groupPath'] for g in groups) or (name=='skirt-arms' and 'スカート' in p['name'])}
        def color(part,point,normal,base):
            if '08 床板' in part['groupPath'] or '02 屋根' in part['groupPath']:return (90,150,185)
            return (170,100,65) if 'スカート' in part['name'] else base
        render(a.exports,report,part_ids=ids,clip_bounds=box,camera=camera,size=(1400,850),studio=True,surface_color=color).save(a.output/(name+'.png'))
        print(name,flush=True)


if __name__=='__main__':main()
