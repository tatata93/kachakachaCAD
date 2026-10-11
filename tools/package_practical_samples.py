"""Package explicitly selected local samples and evidence, without photos by default.

Plan entries use workspace-relative paths and explicit archive destinations.
Every entry is hashed. Refuse traversal, missing files, duplicates, or overwrite.
No network, publication, or changes to the original CAD repository.
"""
import argparse,hashlib,json,zipfile
from pathlib import Path
from datetime import datetime,timezone
p=argparse.ArgumentParser();p.add_argument('--workspace',required=True);p.add_argument('--plan',required=True);p.add_argument('--output',required=True);a=p.parse_args()
root=Path(a.workspace).resolve();plan=json.loads(Path(a.plan).read_text(encoding='utf-8'));out=Path(a.output).resolve()
if out.exists():raise SystemExit('Refusing to replace an existing package')
out.parent.mkdir(parents=True,exist_ok=True);entries=[];seen=set()
for item in plan['files']:
    source=(root/item['source']).resolve();destination=item['destination'].replace('\\','/')
    if not source.is_relative_to(root) or not source.is_file():raise ValueError(f'Invalid source: {source}')
    if destination.startswith('/') or '..' in destination.split('/') or destination in seen:raise ValueError(f'Invalid destination: {destination}')
    seen.add(destination);entries.append((source,destination))
records=[]
with zipfile.ZipFile(out,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    for source,destination in entries:
        data=source.read_bytes();z.writestr(destination,data)
        records.append({'source':str(source.relative_to(root)),'destination':destination,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
    manifest={'created_utc':datetime.now(timezone.utc).isoformat(),'notes':plan.get('notes',[]),'files':records}
    z.writestr('manifest.json',json.dumps(manifest,ensure_ascii=False,indent=2))
with zipfile.ZipFile(out) as z:
    assert z.testzip() is None
    for record in records:assert hashlib.sha256(z.read(record['destination'])).hexdigest()==record['sha256']
out.with_suffix('.sha256').write_text(hashlib.sha256(out.read_bytes()).hexdigest()+'  '+out.name+'\n',encoding='ascii')
print(json.dumps({'package':str(out),'files':len(records),'bytes':out.stat().st_size,'sha256':hashlib.sha256(out.read_bytes()).hexdigest()}))
