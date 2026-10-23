"""Arrange validated STL/STEP by region; translate STL to each part's bed origin."""
from pathlib import Path
import argparse, csv, copy, hashlib, json, re, shutil, struct


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()


def bed_stl(source,target,bounds):
    data=bytearray(source.read_bytes());count=struct.unpack_from('<I',data,80)[0]
    if len(data)!=84+count*50:raise ValueError('invalid binary STL')
    move=[-(bounds[i]+bounds[i+3])/2 for i in [0,1]]+[-bounds[2]]
    for i in range(count):
        for j in [12,24,36]:
            offset=84+i*50+j;point=struct.unpack_from('<3f',data,offset)
            struct.pack_into('<3f',data,offset,*(point[k]+move[k] for k in range(3)))
    target.write_bytes(data);return move


def arrange(source,target,maximum_length=None):
    report=json.loads((source/'print-parts.json').read_text(encoding='utf-8'))
    validation=json.loads((source/'stl-validation.json').read_text(encoding='utf-8'))
    if report['failures'] or validation['failures'] or len(report['parts'])!=validation['parts']:
        raise ValueError('export and mesh validation must both pass')
    result=copy.deepcopy(report);rows=[]
    for index,part in enumerate(result['parts'],1):
        bounds=part['bounds'];size=[bounds[i+3]-bounds[i] for i in range(3)]
        if maximum_length is not None and max(size)>maximum_length:raise ValueError('part exceeds printer design size')
        group=next((x for x in part['groupPath'].split('/') if re.match(r'^\d{2} ',x)),None)
        if group is None:raise ValueError('part has no region group')
        number=group[:2];name=re.sub(r'[<>:"/\\|?*]','_',part['name'])[:32]
        stem=f'P{index:03}-{name}';stl=Path('STL')/number/(stem+'.stl');step=Path('STEP')/number/(stem+'.step')
        (target/stl).parent.mkdir(parents=True,exist_ok=True);(target/step).parent.mkdir(parents=True,exist_ok=True)
        move=bed_stl(source/part['stl'],target/stl,bounds);shutil.copy2(source/part['step'],target/step)
        part.update(assemblyBounds=bounds,bounds=[bounds[i]+move[i%3] for i in range(6)],printTranslationMm=move,
                    stl=stl.as_posix(),step=step.as_posix(),stlSha=sha(target/stl),stepSha=sha(target/step))
        rows.append(dict(number=index,name=part['name'],group=part['groupPath'],xMm=size[0],yMm=size[1],zMm=size[2],
                         volumeMm3=part['volumeMm3'],color=part['manufacturing']['colorName'],stl=stl.as_posix(),step=step.as_posix()))
    result['stlCoordinates']='Individual part: centered XY, minimum Z=0; orientation unchanged. STEP keeps assembled world coordinates.'
    (target/'print-parts.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    with (target/'parts.csv').open('w',encoding='utf-8',newline='') as file:
        writer=csv.DictWriter(file,fieldnames=rows[0].keys());writer.writeheader();writer.writerows(rows)
    return result


def main():
    parser=argparse.ArgumentParser();parser.add_argument('main',type=Path);parser.add_argument('split',type=Path)
    parser.add_argument('samples',type=Path);parser.add_argument('destination',type=Path)
    args=parser.parse_args();args.destination.mkdir(parents=True,exist_ok=False)
    arrange(args.main,args.destination/'full-car');arrange(args.split,args.destination/'small-printer',125)
    for name in ['kuha115-1000-nagano-1-80.kcd2','kuha115-1000-nagano-1-80-split.kcd2','README.md',
                 'three-view.png','front.png','side.png','top.png','model-preview.png','cad-screen.png','index.html',
                 'validation-summary.json','validation-main.json','validation-split.json']:
        source=args.samples/name
        if source.exists():
            if name=='index.html':
                html=source.read_text(encoding='utf-8').replace(
                    '<a href="../../out/models/series115-nagano-print-20261023.zip">部位別STL・STEP ZIP</a>',
                    '<a href="full-car/parts.csv">通常版部品表</a><a href="small-printer/parts.csv">分割版部品表</a>')
                (args.destination/name).write_text(html,encoding='utf-8',newline='\n')
            else:shutil.copy2(source,args.destination/name)
    print(args.destination)


if __name__=='__main__':main()
