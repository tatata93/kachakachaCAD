"""Offline Bambu Studio CLI validation. Never connects to or sends to a printer.

The selected A1/0.4mm/PLA/0.2mm profile is a reproducible test assumption, not
a statement about the user's printer. Original installed profiles are read only.
"""
import argparse,json,subprocess
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('model');p.add_argument('--output',required=True)
p.add_argument('--exe',default='C:/tool/Bambu Studio/bambu-studio.exe')
a=p.parse_args();out=Path(a.output).resolve();out.mkdir(parents=True,exist_ok=True)
root=Path(a.exe).parent/'resources/profiles/BBL'
index={}
for file in root.rglob('*.json'):
    try:
        value=json.loads(file.read_text(encoding='utf-8-sig'))
        if 'name' in value:index[value['name']]=(file,value)
    except (ValueError,UnicodeError):pass

def resolve(name,seen=()):
    if name in seen:raise ValueError('profile inheritance cycle')
    file,value=index[name];result={}
    if value.get('inherits'):result.update(resolve(value['inherits'],seen+(name,)))
    result.update(value);result.pop('inherits',None)
    return result

profiles=[('machine','Bambu Lab A1 0.4 nozzle'),('process','0.20mm Standard @BBL A1'),('filament','Bambu PLA Basic @BBL A1')]
for kind,name in profiles:
    (out/(kind+'.json')).write_text(json.dumps(resolve(name),ensure_ascii=False,indent=2),encoding='utf-8')
command=[a.exe,'--load-settings',str(out/'machine.json')+';'+str(out/'process.json'),
         '--load-filaments',str(out/'filament.json'),'--ensure-on-bed','--arrange','1',
         '--slice','0','--outputdir',str(out),'--export-3mf','sliced.3mf',
         '--export-slicedata',str(out/'layers'),str(Path(a.model).resolve())]
(out/'command.json').write_text(json.dumps(command,indent=2),encoding='utf-8')
with (out/'slice.log').open('wb') as log:
    result=subprocess.run(command,stdout=log,stderr=subprocess.STDOUT)
print(f'Slicer exit {result.returncode}: {out}')
raise SystemExit(result.returncode)
