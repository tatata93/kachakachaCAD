"""Extract single-part native output B-Reps without changing the KCD2 files."""
import argparse,json,hashlib
from pathlib import Path
from zipfile import ZipFile
p=argparse.ArgumentParser();p.add_argument('--input',required=True);p.add_argument('--pattern',required=True)
p.add_argument('--output',required=True);a=p.parse_args();out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
records=[]
for i,path in enumerate(sorted(Path(a.input).glob(a.pattern)),1):
    with ZipFile(path) as z:d=json.loads(z.read('document.json'))
    shapes=[f['definition']['frozenBrep'] for f in d['features'] if f['type']=='freeze_derived' and f['definition'].get('frozenBrep')]
    if len(shapes)!=1:raise RuntimeError(f'{path}: expected one independently exported shape, found {len(shapes)}')
    target=out/f'ideal-part-{i:03}.brep'
    if target.exists():raise RuntimeError(f'{target} already exists')
    target.write_text(shapes[0],encoding='utf-8',newline='\n')
    records.append(dict(source=str(path.resolve()),source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),brep=target.name))
(out/'source-manifest.json').write_text(json.dumps(records,indent=2),encoding='utf-8')
print(f'Extracted {len(records)} independent shapes; originals unchanged')
